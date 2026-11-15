package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's draft as the screen shows it: what the program's numbers say, and which plan a finger is on. */
public class DraftTest {
    /** A picture shown at half its size, 10 pixels in from the left and 20 from the top. */
    private static final Draft.Screen HALF = new Draft.Screen() {
        @Override
        public float x(float pictureX) {
            return pictureX / 2 + 10;
        }

        @Override
        public float y(float pictureY) {
            return pictureY / 2 + 20;
        }
    };

    /** The picture as the screen. */
    private static final Draft.Screen SAME = new Draft.Screen() {
        @Override
        public float x(float pictureX) {
            return pictureX;
        }

        @Override
        public float y(float pictureY) {
            return pictureY;
        }
    };

    private static final int FREE = Draft.FREE;

    /**
     * The program's numbers: open, its building on a deposit or not, the counts, the resting finger on the map, then the places
     * of plans, line and aim.
     */
    private static float[] shown(boolean picksDeposit, float[][] plans, float[][] line, float[] aim) {
        int count = 6 + 8 * (plans.length + line.length + (aim != null ? 1 : 0));
        float[] values = new float[Draft.VALUES];
        values[0] = 1;
        values[1] = picksDeposit ? 1 : 0;
        values[2] = plans.length;
        values[3] = line.length;
        values[4] = aim != null ? 1 : 0;
        values[5] = aim != null ? 1 : 0;
        int at = 6;
        for (float[] place : plans) {
            System.arraycopy(place, 0, values, at, 8);
            at += 8;
        }
        for (float[] place : line) {
            System.arraycopy(place, 0, values, at, 8);
            at += 8;
        }
        if (aim != null) {
            System.arraycopy(aim, 0, values, at, 8);
        }
        assertEquals(count, at + (aim != null ? 8 : 0));
        return values;
    }

    /** A place: its number, its middle, the box its ground takes, its flags. */
    private static float[] place(int id, float x, float y, float half, int flags) {
        return new float[] {id, x, y, x - half, y - half, x + half, y + half, flags};
    }

    private static int count(float[] values) {
        return 6 + 8 * ((int) values[2] + (int) values[3] + (int) values[4]);
    }

    private static Draft draft(float[] values) {
        Draft draft = new Draft();
        draft.update(values, count(values), SAME);
        return draft;
    }

    @Test
    public void aDraftIsClosedWithoutNumbersOrWithAZero() {
        Draft draft = new Draft();
        draft.update(new float[Draft.VALUES], 0, SAME);
        assertFalse(draft.open());
        draft.update(new float[] {0}, 1, SAME);
        assertFalse(draft.open());
        assertEquals(0, draft.plans());
        assertFalse(draft.aimShown());
        assertFalse(draft.restsOnMap());
    }

    @Test
    public void theProgramsPlacesComeOnTheScreen() {
        float[] values = shown(true, new float[][] {place(4, 100, 200, 10, FREE), place(9, 300, 200, 10, Draft.NEWEST | FREE)},
                               new float[][] {place(0, 320, 200, 10, Draft.TAKEN)}, place(0, 600, 100, 6, 0));
        Draft draft = new Draft();
        assertTrue(draft.update(values, count(values), HALF));
        assertTrue(draft.open());
        assertTrue(draft.picksDeposit());
        assertEquals(2, draft.plans());
        assertEquals(1, draft.lineCount());
        assertTrue(draft.aimShown());
        assertTrue(draft.restsOnMap());
        assertEquals(4, draft.id(0));
        assertEquals(60, draft.x(0), 0);
        assertEquals(120, draft.y(0), 0);
        assertEquals(55, draft.left(0), 0);
        assertEquals(115, draft.top(0), 0);
        assertEquals(65, draft.right(0), 0);
        assertEquals(125, draft.bottom(0), 0);
        assertTrue(draft.has(1, Draft.NEWEST));
        assertFalse(draft.has(0, Draft.NEWEST));
        assertEquals(2, draft.lineItem(0));
        assertTrue(draft.has(draft.lineItem(0), Draft.TAKEN));
        assertEquals(3, draft.aimItem());
        assertEquals(310, draft.x(draft.aimItem()), 0);
    }

    @Test
    public void anEmptyDraftPicksNoDepositAndNoFingerRests() {
        Draft draft = new Draft();
        float[] values = shown(false, new float[0][], new float[0][], null);
        draft.update(values, count(values), HALF);
        assertTrue(draft.open());
        assertEquals(0, draft.plans());
        assertFalse(draft.picksDeposit());
        assertFalse("no finger rests", draft.restsOnMap());
    }

    @Test
    public void onlyWholePlacesAreRead() {
        // The program says three plans, but the numbers hold two.
        float[] values = shown(false, new float[][] {place(1, 100, 100, 10, FREE), place(2, 200, 100, 10, FREE)}, new float[0][],
                               null);
        values[2] = 3;
        Draft draft = new Draft();
        draft.update(values, 6 + 2 * 8 + 5, SAME);
        assertEquals(2, draft.plans());
        assertEquals(0, draft.lineCount());
    }

    @Test
    public void anUpdateSaysWhetherAnythingChanged() {
        float[] values = shown(false, new float[][] {place(1, 100, 100, 10, FREE)}, new float[0][], null);
        Draft draft = new Draft();
        assertTrue(draft.update(values, count(values), SAME));
        assertFalse(draft.update(values.clone(), count(values), SAME));
        // The first plan's x: after the head's 6 numbers, its id.
        values[7] = 101;
        assertTrue(draft.update(values, count(values), SAME));
        assertEquals(101, draft.x(0), 0);
    }

    @Test
    public void aFingerIsOnAPlanWithinItsBoxAndTheReach() {
        Draft draft = draft(shown(false, new float[][] {place(5, 100, 100, 10, FREE)}, new float[0][], null));
        assertEquals(5, draft.planAt(100, 100, 15, 0));
        assertEquals(5, draft.planAt(125, 100, 15, 0));
        assertEquals(5, draft.planAt(75, 75, 15, 0));
        assertEquals(0, draft.planAt(126, 100, 15, 0));
        assertEquals(0, draft.planAt(100, 74, 15, 0));
    }

    @Test
    public void theNewestPlanIsFoundFirst() {
        Draft draft = draft(shown(false, new float[][] {place(1, 100, 100, 10, FREE), place(2, 110, 100, 10, Draft.NEWEST | FREE)},
                                  new float[0][], null));
        assertEquals(2, draft.planAt(104, 100, 15, 0));
        assertEquals(1, draft.planAt(80, 100, 15, 0));
    }

    @Test
    public void aSmallPlanIsAtLeastItsLeastAboutItsMiddle() {
        // Far away its ground is a pixel on the screen: a finger finds it within the least half side and the reach.
        Draft draft = draft(shown(false, new float[][] {place(3, 100, 100, 0.5f, FREE)}, new float[0][], null));
        assertEquals(3, draft.planAt(126, 100, 15, 12));
        assertEquals(0, draft.planAt(128, 100, 15, 12));
        assertEquals(0, draft.planAt(117, 100, 15, 0));
    }

    @Test
    public void aPlanUnderAPanelIsNotUnderTheFinger() {
        // A finger there is on the panel, which takes it.
        Draft draft = draft(shown(false, new float[][] {place(1, 100, 100, 10, FREE), place(2, 100, 100, 10, Draft.NEWEST)},
                                  new float[0][], null));
        assertEquals(1, draft.planAt(100, 100, 15, 0));
    }

    @Test
    public void aPlansNumberTellsWhetherItIsTheNewestOrOnADeposit() {
        Draft draft = draft(shown(true, new float[][] {place(1, 100, 100, 10, Draft.ON_DEPOSIT | FREE),
            place(2, 200, 100, 10, Draft.NEWEST | FREE)}, new float[0][], null));
        assertTrue(draft.onDeposit(1));
        assertFalse(draft.onDeposit(2));
        assertTrue(draft.newest(2));
        assertFalse(draft.newest(1));
        assertFalse("a number no plan has", draft.newest(7));
    }

    @Test
    public void theLoupesCopyIsTheSquareAboutTheFingerKeptOnThePicture() {
        // The phone's 768-line picture: 6 mm are 68 pixels. About the finger in the middle of it.
        assertEquals(366, Draft.loupeFrom(400, 68, 768));
        // A finger 10 pixels from the top: the square at the top, its middle 24 pixels below the finger, where the loupe shows it.
        int top = Draft.loupeFrom(10, 68, 768);
        assertEquals(0, top);
        assertEquals(24, top + 68 / 2 - 10);
        // At the bottom and at the right of a 1647 by 768 picture.
        assertEquals(700, Draft.loupeFrom(760, 68, 768));
        assertEquals(1579, Draft.loupeFrom(1640, 68, 1647));
        // A picture no larger than the square.
        assertEquals(0, Draft.loupeFrom(30, 68, 68));
    }
}
