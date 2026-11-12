package io.github.m3rt1n99.fafre;

/**
 * The touch scheme Griff's draft as the screen shows it (buildstage/griff-v3-spec.md, T2): buildings planned on the map before
 * anything is ordered. The program holds the plans at world places (android/native/include/GriffDraft.h) and gives each frame
 * where they are on the picture ({@link Game#griffDraft}); here, in the screen's pixels: whether the draft is open and whether
 * its building stands on a deposit, each plan's place, the box its ground takes and its flags, the line a finger would add, the
 * plan a resting finger would place, and where the tick and the cross stand. And which plan a finger is on.
 */
final class Draft {
    /** How many numbers {@link Game#griffDraft} gives at most. */
    static final int VALUES = 512;
    /** A place's flags, as the program gives them. */
    static final int NEWEST = 1;
    static final int ON_DEPOSIT = 2;
    static final int TAKEN = 4;
    /** Its middle on the free map: not under a panel, which a finger there would be on. */
    static final int FREE = 8;
    /** A plan's box on the screen is at least this wide, for the eye as for a finger (the mockup's 2.3 mm). */
    static final float LEAST_MM = 2.3f;

    // The program's numbers (GriffAndroid.cpp PublishDraft): a head, then each place's.
    private static final int OPEN = 0;
    private static final int PICKS_DEPOSIT = 1;
    private static final int CONSTRUCTION_TOP = 2;
    private static final int COLUMN_LEFT = 3;
    private static final int PLAN_COUNT = 4;
    private static final int LINE_COUNT = 5;
    private static final int AIM_COUNT = 6;
    private static final int RESTS_ON_MAP = 7;
    private static final int HEAD = 8;
    private static final int ITEM = 8;
    private static final int ID = 0;
    private static final int X = 1;
    private static final int Y = 2;
    private static final int LEFT = 3;
    private static final int TOP = 4;
    private static final int RIGHT = 5;
    private static final int BOTTOM = 6;
    private static final int FLAGS = 7;

    /** From the picture's pixels to the screen's. */
    interface Screen {
        float x(float pictureX);

        float y(float pictureY);
    }

    private final float[] mShown = new float[VALUES];
    private int mShownCount;
    private boolean mOpen;
    private boolean mPicksDeposit;
    private float mConstructionTop = -1;
    private float mColumnLeft = -1;
    private int mPlans;
    private int mLine;
    private int mAim;
    private boolean mRestsOnMap;
    /** The places in the screen's pixels: the plans, then the line's, then the aim's, ITEM numbers each. */
    private final float[] mItems = new float[VALUES];

    /**
     * The program's numbers, `count` of them, in the picture's pixels: what is shown from now on. Whether anything changed. Numbers
     * that are not whole places are left out.
     */
    boolean update(float[] values, int count, Screen screen) {
        if (sameAsShown(values, count)) {
            return false;
        }
        System.arraycopy(values, 0, mShown, 0, count);
        mShownCount = count;
        mOpen = count > OPEN && values[OPEN] != 0;
        if (!mOpen || count < HEAD) {
            mPicksDeposit = false;
            mConstructionTop = -1;
            mColumnLeft = -1;
            mPlans = 0;
            mLine = 0;
            mAim = 0;
            mRestsOnMap = false;
            return true;
        }
        mPicksDeposit = values[PICKS_DEPOSIT] != 0;
        mRestsOnMap = values[RESTS_ON_MAP] != 0;
        mConstructionTop = values[CONSTRUCTION_TOP] < 0 ? -1 : screen.y(values[CONSTRUCTION_TOP]);
        mColumnLeft = values[COLUMN_LEFT] < 0 ? -1 : screen.x(values[COLUMN_LEFT]);
        int room = (count - HEAD) / ITEM;
        mPlans = Math.max(0, Math.min((int) values[PLAN_COUNT], room));
        mLine = Math.max(0, Math.min((int) values[LINE_COUNT], room - mPlans));
        mAim = Math.max(0, Math.min((int) values[AIM_COUNT], room - mPlans - mLine));
        for (int item = 0; item < mPlans + mLine + mAim; ++item) {
            int from = HEAD + item * ITEM;
            int to = item * ITEM;
            mItems[to + ID] = values[from + ID];
            mItems[to + X] = screen.x(values[from + X]);
            mItems[to + Y] = screen.y(values[from + Y]);
            mItems[to + LEFT] = screen.x(values[from + LEFT]);
            mItems[to + TOP] = screen.y(values[from + TOP]);
            mItems[to + RIGHT] = screen.x(values[from + RIGHT]);
            mItems[to + BOTTOM] = screen.y(values[from + BOTTOM]);
            mItems[to + FLAGS] = values[from + FLAGS];
        }
        return true;
    }

    private boolean sameAsShown(float[] values, int count) {
        if (count != mShownCount) {
            return false;
        }
        for (int at = 0; at < count; ++at) {
            if (Float.compare(values[at], mShown[at]) != 0) {
                return false;
            }
        }
        return true;
    }

    boolean open() {
        return mOpen;
    }

    /** Whether the building of the next plans stands on a deposit: a box with Box gives each free deposit in it a plan. */
    boolean picksDeposit() {
        return mPicksDeposit;
    }

    /** The top of FAF's construction panel, which the tick and the cross stand above; below 0 where it is not shown. */
    float constructionTop() {
        return mConstructionTop;
    }

    /** The left of the column at the right (FAF's avatars), which the tick stands left of; below 0 where it is not shown. */
    float columnLeft() {
        return mColumnLeft;
    }

    int plans() {
        return mPlans;
    }

    int lineCount() {
        return mLine;
    }

    boolean aimShown() {
        return mAim > 0;
    }

    /** Whether the finger that rests is on the map, not on FAF's UI: the loupe is for the map. */
    boolean restsOnMap() {
        return mRestsOnMap;
    }

    /** The plans are places 0 to plans() - 1, the line's follow, then the aim's. */
    int lineItem(int at) {
        return mPlans + at;
    }

    int aimItem() {
        return mPlans + mLine;
    }

    int id(int item) {
        return (int) mItems[item * ITEM + ID];
    }

    float x(int item) {
        return mItems[item * ITEM + X];
    }

    float y(int item) {
        return mItems[item * ITEM + Y];
    }

    float left(int item) {
        return mItems[item * ITEM + LEFT];
    }

    float top(int item) {
        return mItems[item * ITEM + TOP];
    }

    float right(int item) {
        return mItems[item * ITEM + RIGHT];
    }

    float bottom(int item) {
        return mItems[item * ITEM + BOTTOM];
    }

    boolean has(int item, int flag) {
        return ((int) mItems[item * ITEM + FLAGS] & flag) != 0;
    }

    /**
     * The plan a finger here is on, the newest first (the mockup's planAt): within its box, at least `least` each way from its
     * middle, and `reach` beyond; only one whose middle is on the free map, as a finger on a panel over a plan is on the panel.
     * Its number, or 0.
     */
    int planAt(float fingerX, float fingerY, float reach, float least) {
        for (int plan = mPlans - 1; plan >= 0; --plan) {
            if (!has(plan, FREE)) {
                continue;
            }
            float middleX = x(plan);
            float middleY = y(plan);
            float left = Math.min(left(plan), middleX - least) - reach;
            float right = Math.max(right(plan), middleX + least) + reach;
            float top = Math.min(top(plan), middleY - least) - reach;
            float bottom = Math.max(bottom(plan), middleY + least) + reach;
            if (fingerX >= left && fingerX <= right && fingerY >= top && fingerY <= bottom) {
                return id(plan);
            }
        }
        return 0;
    }

    /** The plan numbered `id`'s place among the plans, or -1. */
    private int planNumbered(int id) {
        for (int plan = 0; plan < mPlans; ++plan) {
            if (id(plan) == id) {
                return plan;
            }
        }
        return -1;
    }

    /** Whether the plan numbered `id` is the newest, from which a drag draws a line. */
    boolean newest(int id) {
        int plan = planNumbered(id);
        return plan >= 0 && has(plan, NEWEST);
    }

    /** Whether the plan numbered `id` stands on a deposit: it neither starts a line nor moves with the draft. */
    boolean onDeposit(int id) {
        int plan = planNumbered(id);
        return plan >= 0 && has(plan, ON_DEPOSIT);
    }
}
