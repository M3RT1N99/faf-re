package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Before;
import org.junit.Test;

/** How the picture lies on the screen, with the phone's numbers: 1647x768 on 1544x720, and the lobby's panel. */
public class PageViewTest {
    private static final float NEAR = 0.51f;
    private static final float LOBBY_SCALE = 1544f / 1024;
    private final PageView view = new PageView();

    @Before
    public void setUp() {
        view.setScreen(1544, 720);
        view.setPicture(1647, 768);
    }

    private void lobby() {
        view.setPage(312, 0, 1336, 768);
    }

    /** A page as wide as the picture and higher than the screen, which is no zoom until the picture grows. */
    private void pageThatIsNoZoom() {
        view.setPage(0, 0, 1647, 1400);
        assertFalse(view.zoomed());
    }

    /** Where such a page stands once it is a zoom: viewTop() says 0 for as long as it is not. */
    private float whereItStandsOnceAZoom() {
        view.setPicture(3000, 1400);
        assertTrue(view.zoomed());
        return view.viewTop();
    }

    @Test
    public void withoutAPageThePictureFillsTheScreen() {
        assertFalse(view.zoomed());
        assertEquals(1544, view.viewWidth());
        assertEquals(720, view.viewHeight());
        assertEquals(0, view.viewLeft(), 0);
        assertEquals(0, view.viewTop(), 0);
        assertEquals(1, view.zoom(), 0.001);
        assertEquals(824, view.pictureX(772));
        assertEquals(384, view.pictureY(360));
    }

    @Test
    public void aPageFillsTheScreensWidthAtItsLeftEdgeFromItsTop() {
        lobby();
        assertTrue(view.zoomed());
        assertEquals(LOBBY_SCALE, view.scale(), 0.001);
        assertEquals(Math.round(1647 * LOBBY_SCALE), view.viewWidth());
        assertEquals(Math.round(768 * LOBBY_SCALE), view.viewHeight());
        assertEquals(-Math.round(312 * LOBBY_SCALE), view.viewLeft(), 0);
        assertEquals(0, view.viewTop(), 0);
        assertEquals(1.608, view.zoom(), 0.001);
    }

    @Test
    public void theScreensEdgesAreThePagesEdges() {
        lobby();
        assertEquals(312, view.pictureX(0), 1);
        assertEquals(1336, view.pictureX(1544), 1);
        assertEquals(0, view.pictureY(0));
        assertEquals(Math.round(720 / LOBBY_SCALE), view.pictureY(720));
    }

    @Test
    public void aFingerScrollsWithinThePagesHeight() {
        lobby();
        float range = 768 * LOBBY_SCALE - 720;
        view.scrollBy(100);
        assertEquals("not above the page's top", 0, view.viewTop(), 0);
        view.scrollBy(-100);
        assertEquals(-100, view.viewTop(), 0);
        assertEquals(Math.round(100 / LOBBY_SCALE), view.pictureY(0));
        view.scrollBy(-5000);
        assertEquals(-range, view.viewTop(), NEAR);
        assertEquals(767, view.pictureY(720));
        view.scrollBy(50);
        assertEquals(-range + 50, view.viewTop(), NEAR);
    }

    @Test
    public void thePlaceIsAlwaysOnThePicture() {
        lobby();
        assertEquals(0, view.pictureY(-50));
        assertEquals(1646, view.pictureX(5000));
        assertEquals(0, view.pictureX(-3000));
    }

    @Test
    public void aPageWithinThePicturesHeightScrollsOnlyThroughItself() {
        view.setPage(336, 17, 1310, 751);
        float scale = 1544f / 974;
        assertEquals(-17 * scale, view.viewTop(), NEAR);
        assertEquals(17, view.pictureY(0));
        view.scrollBy(-5000);
        assertEquals(751, view.pictureY(720));
    }

    @Test
    public void aPageLowerThanTheScreenStandsInTheMiddleAndDoesNotScroll() {
        view.setPage(500, 300, 1100, 500);
        float scale = 1544f / 600;
        float middle = (720 - 200 * scale) / 2 - 300 * scale;
        assertEquals(middle, view.viewTop(), NEAR);
        view.scrollBy(100);
        assertEquals(middle, view.viewTop(), NEAR);
        view.scrollBy(-100);
        assertEquals(middle, view.viewTop(), NEAR);
    }

    @Test
    public void aPageAsWideAsThePictureIsNoZoom() {
        view.setPage(0, 0, 1647, 768);
        assertFalse(view.zoomed());
        assertEquals(1544, view.viewWidth());
    }

    @Test
    public void aFingerMovesNoPageThatIsNoZoom() {
        pageThatIsNoZoom();
        view.scrollBy(-100);
        assertEquals(0, whereItStandsOnceAZoom(), 0);
    }

    @Test
    public void theSamePageAgainChangesNothing() {
        lobby();
        view.scrollBy(-100);
        assertFalse(view.setPage(312, 0, 1336, 768));
        assertEquals(-100, view.viewTop(), 0);
    }

    @Test
    public void closingThePageTakesTheViewBack() {
        lobby();
        view.scrollBy(-100);
        assertTrue(view.clearPage());
        assertFalse(view.clearPage());
        assertFalse(view.zoomed());
        assertEquals(0, view.viewTop(), 0);
        assertEquals(0, view.viewLeft(), 0);
        assertEquals(1544, view.viewWidth());
    }

    @Test
    public void aPageOpensAtItsTopEvenIfTheLastStoodLower() {
        lobby();
        view.scrollBy(-100);
        view.clearPage();
        lobby();
        assertEquals(0, view.viewTop(), 0);
    }

    @Test
    public void aPageComesBackAsItStoodWhenOneOpenedOverIt() {
        lobby();
        view.scrollBy(-100);
        view.setPage(346, 38, 1302, 730);
        lobby();
        assertEquals(-100, view.viewTop(), 0);
    }

    @Test
    public void pagesNestAndComeBackAsTheyStood() {
        lobby();
        view.scrollBy(-100);
        view.setPage(346, 38, 1302, 730);
        view.scrollBy(-50);
        float second = view.viewTop();
        view.setPage(500, 0, 1147, 768);
        view.scrollBy(-20);
        view.setPage(346, 38, 1302, 730);
        assertEquals(second, view.viewTop(), 0);
        lobby();
        assertEquals(-100, view.viewTop(), 0);
    }

    @Test
    public void aPageThatHasClosedOpensAgainAtItsTop() {
        lobby();
        view.setPage(346, 38, 1302, 730);
        float top = view.viewTop();
        view.scrollBy(-50);
        lobby();
        assertTrue(view.setPage(346, 38, 1302, 730));
        assertEquals(top, view.viewTop(), 0);
    }

    @Test
    public void aPageUnderTwoThatComesBackClosesThemBoth() {
        lobby();
        view.setPage(346, 38, 1302, 730);
        float top = view.viewTop();
        view.scrollBy(-50);
        view.setPage(500, 0, 1147, 768);
        lobby();
        view.setPage(346, 38, 1302, 730);
        assertEquals(top, view.viewTop(), 0);
    }

    @Test
    public void thePageWaitsForTheScreenAndThePicture() {
        PageView early = new PageView();
        early.setPage(312, 0, 1336, 768);
        assertFalse(early.zoomed());
        early.setScreen(1544, 720);
        assertFalse(early.zoomed());
        early.setPicture(1647, 768);
        assertTrue(early.zoomed());
        assertEquals(0, early.viewTop(), 0);
        assertEquals(LOBBY_SCALE, early.scale(), 0.001);
    }

    @Test
    public void withTheKeyboardUpThePageGoesUpFurther() {
        lobby();
        view.setKeyboard(360);
        view.scrollBy(-5000);
        assertEquals(767, view.pictureY(360));
        view.setKeyboard(0);
        assertEquals(767, view.pictureY(720));
    }

    @Test
    public void aFieldUnderTheKeyboardComesAboveIt() {
        lobby();
        view.setKeyboard(360);
        view.reveal(684, 706);
        float bottom = view.viewTop() + 706 * LOBBY_SCALE;
        assertTrue("above the keyboard", bottom <= 360);
        assertTrue("not far from it", bottom > 360 - 30);
    }

    @Test
    public void aFieldThatCanBeSeenStaysWhereItIs() {
        lobby();
        view.setKeyboard(360);
        view.reveal(100, 120);
        assertEquals(0, view.viewTop(), 0);
    }

    @Test
    public void aFieldAboveTheScreenComesDown() {
        lobby();
        view.scrollBy(-400);
        view.reveal(100, 120);
        assertTrue(view.viewTop() + 100 * LOBBY_SCALE > 0);
    }

    @Test
    public void aFieldOnAPageThatIsNoZoomMovesNothing() {
        pageThatIsNoZoom();
        view.setKeyboard(360);
        view.reveal(684, 706);
        view.setKeyboard(0);
        assertEquals(0, whereItStandsOnceAZoom(), 0);
    }
}
