"""Builds a plain ONNX graph for a small MLP from numpy weights.

Writing the graph directly (MatMul/Add/Relu/Softmax) keeps the exported models
tiny, deterministic and independent of the PyTorch exporter version. Input
standardisation is baked in so the C++ side feeds raw features.
"""

from __future__ import annotations

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

OPSET = 13


def build_mlp(
    weights: list[tuple[np.ndarray, np.ndarray]],
    input_name: str,
    input_dim: int,
    output_name: str,
    mean: np.ndarray,
    std: np.ndarray,
    softmax: bool,
    metadata: dict[str, str],
    input_shape: list | None = None,
) -> onnx.ModelProto:
    """weights: [(W (in,out), b (out,)), ...]; ReLU between layers.

    input_shape: optional shape of the model input (e.g. ["N", 16, 2]); it is
    flattened to (N, input_dim) inside the graph.
    """
    nodes, inits = [], []

    def const(name, arr):
        inits.append(numpy_helper.from_array(np.asarray(arr, dtype=np.float32), name))
        return name

    shape = input_shape or ["N", input_dim]
    x = input_name
    if len(shape) != 2:
        inits.append(numpy_helper.from_array(np.array([-1, input_dim], dtype=np.int64), "flat_shape"))
        nodes.append(helper.make_node("Reshape", [x, "flat_shape"], ["x_flat"]))
        x = "x_flat"

    nodes.append(helper.make_node("Sub", [x, const("mean", mean)], ["x_c"]))
    nodes.append(helper.make_node("Div", ["x_c", const("std", std)], ["x_n"]))
    h = "x_n"
    for i, (w, b) in enumerate(weights):
        last = i == len(weights) - 1
        nodes.append(helper.make_node("MatMul", [h, const(f"W{i}", w)], [f"mm{i}"]))
        out = (f"logits" if softmax else output_name) if last else f"z{i}"
        nodes.append(helper.make_node("Add", [f"mm{i}", const(f"b{i}", b)], [out]))
        if not last:
            nodes.append(helper.make_node("Relu", [out], [f"h{i}"]))
            h = f"h{i}"
    if softmax:
        nodes.append(helper.make_node("Softmax", ["logits"], [output_name], axis=-1))

    out_dim = weights[-1][0].shape[1]
    graph = helper.make_graph(
        nodes,
        "mausely_mlp",
        [helper.make_tensor_value_info(input_name, TensorProto.FLOAT, shape)],
        [helper.make_tensor_value_info(output_name, TensorProto.FLOAT, ["N", out_dim])],
        inits,
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", OPSET)], producer_name="mausely-train")
    model.ir_version = 8
    for k, v in metadata.items():
        entry = model.metadata_props.add()
        entry.key, entry.value = k, str(v)
    onnx.checker.check_model(model)
    return model


def torch_mlp_weights(layers) -> list[tuple[np.ndarray, np.ndarray]]:
    """Extracts (W^T, b) pairs from a sequence of torch.nn.Linear layers."""
    return [(l.weight.detach().cpu().numpy().T.copy(), l.bias.detach().cpu().numpy().copy()) for l in layers]
