package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

/** The picture's size for a screen (PictureSize): its lines, and a width that keeps the view's shape. */
public class PictureSizeTest {
    @Test
    public void aPictureKeepsTheViewsShapeAtTheChosenLines() {
        int[] size = PictureSize.of(2400, 1080, 768);
        assertEquals(1707, size[0]);
        assertEquals(768, size[1]);
    }

    @Test
    public void aPictureHasTheLeastLinesOnAScreenWithFewer() {
        int[] size = PictureSize.of(1544, 720, 768);
        assertEquals(1647, size[0]);
        assertEquals(768, size[1]);
        size = PictureSize.of(1544, 720, PictureSize.AS_THE_SCREEN);
        assertEquals(768, size[1]);
    }

    @Test
    public void aPictureIsWideEnoughInANarrowWindow() {
        int[] size = PictureSize.of(1195, 1080, 768);
        assertEquals(1024, size[0]);
        assertEquals(925, size[1]);
    }

    @Test
    public void aPictureAsTheScreenHasItsSize() {
        int[] size = PictureSize.of(3088, 1440, PictureSize.AS_THE_SCREEN);
        assertEquals(3088, size[0]);
        assertEquals(1440, size[1]);
    }
}
