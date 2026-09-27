package io.github.mahdidigitalx_oss.mausely

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsTextTest {
    @Test
    fun parsesTheDesktopSettingsFormat() {
        val map = SettingsText.parse("# Mausely settings\ncamera_index=1\r\n  mirror = 0 \n; comment\njunk line\nregion_x0=0.250000\n")
        assertEquals(mapOf("camera_index" to "1", "mirror" to "0", "region_x0" to "0.250000"), map)
    }

    @Test
    fun formatRoundTrips() {
        val map = linkedMapOf("a" to "1", "b" to "0.5")
        assertEquals("a=1\nb=0.5\n", SettingsText.format(map))
        assertEquals(map, SettingsText.parse(SettingsText.format(map)))
    }

    @Test
    fun typedAccessorsFallBackToDefaults() {
        val map = mapOf("f" to "0.750000", "i" to "3.000000", "on" to "1", "off" to "0", "bad" to "x")
        assertEquals(0.75f, map.float("f"), 1e-6f)
        assertEquals(3, map.int("i"))
        assertEquals(7, map.int("bad", 7))
        assertEquals(2f, map.float("missing", 2f), 0f)
        assertTrue(map.flag("on"))
        assertFalse(map.flag("off"))
        assertFalse(map.flag("missing"))
    }
}
