/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuState.h"

#include "Npc/SOLNpcScaleMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of two menu states matches
    bool MenuStateTestEqual(const SOLMenuState::FMenuState& a, const SOLMenuState::FMenuState& b)
    {
        return a.Mode == b.Mode && a.Tab == b.Tab && a.bMapOpen == b.bMapOpen;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a Playing state reached through the public API from the initial state
    SOLMenuState::FMenuState MenuStateTestPlaying()
    {
        using namespace SOLMenuState;
        return Apply(Initial(), EMenuEvent::PlayPressed);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a Paused state reached through the public API (Play, then Escape)
    SOLMenuState::FMenuState MenuStateTestPaused()
    {
        using namespace SOLMenuState;
        return Apply(MenuStateTestPlaying(), EMenuEvent::EscapePressed);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateInitialTest, "SOLTest.MenuState.Initial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The game starts in the main menu on the Resume tab with the map closed, matching a default-constructed state
bool FSOLMenuStateInitialTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState initial = Initial();
    TestTrue(TEXT("Initial mode is MainMenu"), initial.Mode == EMenuMode::MainMenu);
    TestTrue(TEXT("Initial tab is Resume"), initial.Tab == EMenuTab::Resume);
    TestFalse(TEXT("Initial map is closed"), initial.bMapOpen);
    TestTrue(TEXT("Initial matches a default-constructed state"), MenuStateTestEqual(initial, FMenuState()));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStatePlayPressedTest, "SOLTest.MenuState.PlayPressed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Play moves the main menu to Playing and is ignored in every other mode
bool FSOLMenuStatePlayPressedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState playing = Apply(Initial(), EMenuEvent::PlayPressed);
    TestTrue(TEXT("MainMenu -> Playing"), playing.Mode == EMenuMode::Playing);
    TestFalse(TEXT("Map still closed after Play"), playing.bMapOpen);

    TestTrue(TEXT("Play while Playing is ignored"),
        MenuStateTestEqual(Apply(playing, EMenuEvent::PlayPressed), playing));
    const FMenuState paused = MenuStateTestPaused();
    TestTrue(TEXT("Play while Paused is ignored"), MenuStateTestEqual(Apply(paused, EMenuEvent::PlayPressed), paused));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateEscapeTest, "SOLTest.MenuState.Escape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Escape toggles Playing and Paused, opening the pause menu on the Resume tab, and does nothing in the main menu
bool FSOLMenuStateEscapeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState paused = Apply(MenuStateTestPlaying(), EMenuEvent::EscapePressed);
    TestTrue(TEXT("Playing -> Paused"), paused.Mode == EMenuMode::Paused);
    TestTrue(TEXT("Pause opens on the Resume tab"), paused.Tab == EMenuTab::Resume);

    const FMenuState resumed = Apply(paused, EMenuEvent::EscapePressed);
    TestTrue(TEXT("Paused -> Playing"), resumed.Mode == EMenuMode::Playing);

    const FMenuState mainMenu = Initial();
    TestTrue(TEXT("Escape in the main menu is ignored"),
        MenuStateTestEqual(Apply(mainMenu, EMenuEvent::EscapePressed), mainMenu));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateEscapeResetsTabTest, "SOLTest.MenuState.EscapeResetsTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Re-opening the pause menu always lands on the Resume tab, whatever tab was last selected
bool FSOLMenuStateEscapeResetsTabTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    FMenuState state = SelectTab(MenuStateTestPaused(), EMenuTab::Settings);
    TestTrue(TEXT("Settings tab selected while paused"), state.Tab == EMenuTab::Settings);
    state = Apply(state, EMenuEvent::EscapePressed);
    TestTrue(TEXT("Back to Playing"), state.Mode == EMenuMode::Playing);
    state = Apply(state, EMenuEvent::EscapePressed);
    TestTrue(TEXT("Paused again"), state.Mode == EMenuMode::Paused);
    TestTrue(TEXT("Tab reset to Resume on pause"), state.Tab == EMenuTab::Resume);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateEscapeWithMapTest, "SOLTest.MenuState.EscapeWithMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// While the map is open the map consumes Escape, so the pause menu does not open
bool FSOLMenuStateEscapeWithMapTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState withMap = Apply(MenuStateTestPlaying(), EMenuEvent::MapOpened);
    TestTrue(TEXT("Map opened while Playing"), withMap.bMapOpen);

    const FMenuState afterEscape = Apply(withMap, EMenuEvent::EscapePressed);
    TestTrue(TEXT("Escape with the map open stays Playing"), afterEscape.Mode == EMenuMode::Playing);

    // Once the map is closed, Escape pauses again
    const FMenuState mapClosed = Apply(withMap, EMenuEvent::MapClosed);
    TestFalse(TEXT("Map closed"), mapClosed.bMapOpen);
    TestTrue(TEXT("Escape after closing the map pauses"),
        Apply(mapClosed, EMenuEvent::EscapePressed).Mode == EMenuMode::Paused);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateResumeAndQuitTest, "SOLTest.MenuState.ResumeAndQuit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Resume returns Paused to Playing; Quit to menu returns Paused to the main menu with the map closed
bool FSOLMenuStateResumeAndQuitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState paused = MenuStateTestPaused();
    TestTrue(TEXT("Resume: Paused -> Playing"), Apply(paused, EMenuEvent::ResumePressed).Mode == EMenuMode::Playing);

    const FMenuState quit = Apply(paused, EMenuEvent::QuitToMenuPressed);
    TestTrue(TEXT("Quit: Paused -> MainMenu"), quit.Mode == EMenuMode::MainMenu);
    TestFalse(TEXT("Quit leaves the map closed"), quit.bMapOpen);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateIllegalEventsTest, "SOLTest.MenuState.IllegalEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Resume and Quit to menu outside the pause menu, and map events outside Playing, leave the state unchanged
bool FSOLMenuStateIllegalEventsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState mainMenu = Initial();
    const FMenuState playing = MenuStateTestPlaying();
    const FMenuState paused = MenuStateTestPaused();

    TestTrue(TEXT("Resume in MainMenu ignored"), MenuStateTestEqual(Apply(mainMenu, EMenuEvent::ResumePressed), mainMenu));
    TestTrue(TEXT("Resume in Playing ignored"), MenuStateTestEqual(Apply(playing, EMenuEvent::ResumePressed), playing));
    TestTrue(TEXT("Quit to menu in MainMenu ignored"),
        MenuStateTestEqual(Apply(mainMenu, EMenuEvent::QuitToMenuPressed), mainMenu));
    TestTrue(TEXT("Quit to menu in Playing ignored"),
        MenuStateTestEqual(Apply(playing, EMenuEvent::QuitToMenuPressed), playing));

    TestTrue(TEXT("MapOpened in MainMenu ignored"), MenuStateTestEqual(Apply(mainMenu, EMenuEvent::MapOpened), mainMenu));
    TestTrue(TEXT("MapOpened in Paused ignored"), MenuStateTestEqual(Apply(paused, EMenuEvent::MapOpened), paused));
    TestTrue(TEXT("MapClosed in MainMenu ignored"), MenuStateTestEqual(Apply(mainMenu, EMenuEvent::MapClosed), mainMenu));
    TestTrue(TEXT("MapClosed in Paused ignored"), MenuStateTestEqual(Apply(paused, EMenuEvent::MapClosed), paused));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateMapEventsTest, "SOLTest.MenuState.MapEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Map open/close only touch bMapOpen while Playing and are idempotent
bool FSOLMenuStateMapEventsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState playing = MenuStateTestPlaying();
    const FMenuState opened = Apply(playing, EMenuEvent::MapOpened);
    TestTrue(TEXT("Map open"), opened.bMapOpen);
    TestTrue(TEXT("Mode unchanged by MapOpened"), opened.Mode == EMenuMode::Playing);
    TestTrue(TEXT("Tab unchanged by MapOpened"), opened.Tab == playing.Tab);
    TestTrue(TEXT("MapOpened twice is idempotent"), MenuStateTestEqual(Apply(opened, EMenuEvent::MapOpened), opened));

    const FMenuState closed = Apply(opened, EMenuEvent::MapClosed);
    TestFalse(TEXT("Map closed"), closed.bMapOpen);
    TestTrue(TEXT("MapClosed twice is idempotent"), MenuStateTestEqual(Apply(closed, EMenuEvent::MapClosed), closed));
    TestTrue(TEXT("Open then close returns to the original state"), MenuStateTestEqual(closed, playing));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateSelectTabTest, "SOLTest.MenuState.SelectTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Tabs can be selected in the main menu and pause menu (any tab value, only Tab changes) but not while Playing
bool FSOLMenuStateSelectTabTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState paused = MenuStateTestPaused();
    const FMenuState info = SelectTab(paused, EMenuTab::Info);
    TestTrue(TEXT("Paused: Info selected"), info.Tab == EMenuTab::Info);
    TestTrue(TEXT("Paused: mode unchanged"), info.Mode == EMenuMode::Paused);
    TestTrue(TEXT("Paused: Quit tab just sets Tab"), SelectTab(paused, EMenuTab::Quit).Tab == EMenuTab::Quit);
    TestTrue(TEXT("Paused: Quit tab does not change mode"), SelectTab(paused, EMenuTab::Quit).Mode == EMenuMode::Paused);
    TestTrue(TEXT("Paused: Resume tab does not resume"), SelectTab(info, EMenuTab::Resume).Mode == EMenuMode::Paused);

    const FMenuState about = SelectTab(Initial(), EMenuTab::About);
    TestTrue(TEXT("MainMenu: About selected"), about.Tab == EMenuTab::About);
    TestTrue(TEXT("MainMenu: mode unchanged"), about.Mode == EMenuMode::MainMenu);

    const FMenuState playing = MenuStateTestPlaying();
    TestTrue(TEXT("Playing: selection ignored"), MenuStateTestEqual(SelectTab(playing, EMenuTab::Controls), playing));
    TestTrue(TEXT("Selecting the same tab twice is idempotent"),
        MenuStateTestEqual(SelectTab(info, EMenuTab::Info), info));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateInputAndCursorTest, "SOLTest.MenuState.InputAndCursor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Flight input is suspended in menus; the cursor shows in menus and while the map is open in Playing
bool FSOLMenuStateInputAndCursorTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState mainMenu = Initial();
    const FMenuState playing = MenuStateTestPlaying();
    const FMenuState withMap = Apply(playing, EMenuEvent::MapOpened);
    const FMenuState paused = MenuStateTestPaused();

    TestTrue(TEXT("MainMenu suspends flight input"), SuspendsFlightInput(mainMenu));
    TestTrue(TEXT("Paused suspends flight input"), SuspendsFlightInput(paused));
    TestFalse(TEXT("Playing does not suspend flight input"), SuspendsFlightInput(playing));

    TestTrue(TEXT("MainMenu shows the cursor"), ShouldShowCursor(mainMenu));
    TestTrue(TEXT("Paused shows the cursor"), ShouldShowCursor(paused));
    TestFalse(TEXT("Playing hides the cursor"), ShouldShowCursor(playing));
    TestTrue(TEXT("Playing with the map open shows the cursor"), ShouldShowCursor(withMap));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateSpawnShipTest, "SOLTest.MenuState.SpawnShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ship spawns exactly on the MainMenu -> Playing transition, not on resume or other transitions
bool FSOLMenuStateSpawnShipTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FMenuState mainMenu = Initial();
    const FMenuState playing = MenuStateTestPlaying();
    const FMenuState paused = MenuStateTestPaused();

    TestTrue(TEXT("MainMenu -> Playing spawns"), ShouldSpawnShip(mainMenu, playing));
    TestFalse(TEXT("Paused -> Playing does not spawn"), ShouldSpawnShip(paused, playing));
    TestFalse(TEXT("Playing -> Playing does not spawn"), ShouldSpawnShip(playing, playing));
    TestFalse(TEXT("MainMenu -> MainMenu does not spawn"), ShouldSpawnShip(mainMenu, mainMenu));
    TestFalse(TEXT("Playing -> Paused does not spawn"), ShouldSpawnShip(playing, paused));
    TestFalse(TEXT("Paused -> MainMenu does not spawn"), ShouldSpawnShip(paused, mainMenu));

    // A second session after Quit to menu spawns again
    const FMenuState quit = Apply(paused, EMenuEvent::QuitToMenuPressed);
    TestTrue(TEXT("Replay after quit spawns"), ShouldSpawnShip(quit, Apply(quit, EMenuEvent::PlayPressed)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDeterminismTest, "SOLTest.MenuState.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Apply is pure: the input state is not modified and the same input always yields the same output
bool FSOLMenuStateDeterminismTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const EMenuEvent events[] = { EMenuEvent::PlayPressed, EMenuEvent::MapOpened, EMenuEvent::EscapePressed,
        EMenuEvent::MapClosed, EMenuEvent::EscapePressed, EMenuEvent::ResumePressed, EMenuEvent::EscapePressed,
        EMenuEvent::QuitToMenuPressed };

    // Run the same event sequence twice and compare every intermediate state
    FMenuState runA = Initial();
    FMenuState runB = Initial();
    for (const EMenuEvent menuEvent : events)
    {
        const FMenuState snapshot = runA;
        const FMenuState input = runA;
        const FMenuState output = Apply(input, menuEvent);
        TestTrue(TEXT("Input state not modified"), MenuStateTestEqual(input, snapshot));
        runA = output;
        runB = Apply(runB, menuEvent);
        TestTrue(TEXT("Same input, same output"), MenuStateTestEqual(runA, runB));
    }

    // Sequence: Play, map open, Esc (consumed), map closed, Esc (pause), Resume, Esc (pause), Quit -> main menu
    TestTrue(TEXT("Sequence ends in MainMenu"), runA.Mode == EMenuMode::MainMenu);
    TestFalse(TEXT("Sequence ends with the map closed"), runA.bMapOpen);
    return true;
}

// Demo page tests (issue #11, Part 10e), written against the 10e contract in
// Docs/Plans/11-scale-and-performance-demo-plan.md before any production code existed.

namespace
{
    // Number of rows that are EDemoOption rows (the extra rows follow them)
    constexpr int32 MENUSTATE_OPTION_ROWS = static_cast<int32>(SOLNpcScale::EDemoOption::Count);

    // Number of extra rows after the option rows
    constexpr int32 MENUSTATE_EXTRA_ROWS = static_cast<int32>(SOLMenuState::EDemoExtraRow::Count);

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every option, the seed, the combat model and the ramp flag of two Demo pages match
    bool MenuStateTestPageEqual(const SOLMenuState::FDemoPage& a, const SOLMenuState::FDemoPage& b)
    {
        // Compare every menu option through the public getter, then the fields that are not menu options
        for (int32 option = 0; option < MENUSTATE_OPTION_ROWS; ++option)
        {
            const SOLNpcScale::EDemoOption id = static_cast<SOLNpcScale::EDemoOption>(option);
            if (SOLNpcScale::GetOption(a.Options, id) != SOLNpcScale::GetOption(b.Options, id))
            {
                return false;
            }
        }
        return a.Options.Seed == b.Options.Seed && a.CombatModel == b.CombatModel && a.bRamp == b.bRamp;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Demo-page row of an option
    int32 MenuStateTestOptionRow(SOLNpcScale::EDemoOption option)
    {
        return static_cast<int32>(option);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Demo-page row of an extra row, computed from the contract (after the option rows)
    int32 MenuStateTestExtraRow(SOLMenuState::EDemoExtraRow extra)
    {
        return MENUSTATE_OPTION_ROWS + static_cast<int32>(extra);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a sanitized page with the seed, combat model, ramp flag and several options away from their defaults
    SOLMenuState::FDemoPage MenuStateTestCustomPage()
    {
        using SOLNpcScale::EDemoOption;
        SOLMenuState::FDemoPage page = SOLMenuState::DefaultDemoPage();
        page.Options = SOLNpcScale::SetOption(page.Options, EDemoOption::NpcCount, 10000.0);
        page.Options = SOLNpcScale::SetOption(page.Options, EDemoOption::LaneFraction, 0.7);
        page.Options = SOLNpcScale::SetOption(page.Options, EDemoOption::BattleCount, 2.0);
        page.Options = SOLNpcScale::SetOption(page.Options, EDemoOption::NpcCombat, 0.0);
        page.Options = SOLNpcScale::SetOption(page.Options, EDemoOption::ShowStats, 0.0);
        page.Options.Seed = 777u;
        page.CombatModel = 1;
        page.bRamp = true;
        return page;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every option except the listed ones has the same value in both option sets
    bool MenuStateTestOtherOptionsEqual(const SOLNpcScale::FDemoOptions& a, const SOLNpcScale::FDemoOptions& b,
        TConstArrayView<SOLNpcScale::EDemoOption> skipped)
    {
        // Walk every option, skipping the ones the caller expects to change
        for (int32 option = 0; option < MENUSTATE_OPTION_ROWS; ++option)
        {
            const SOLNpcScale::EDemoOption id = static_cast<SOLNpcScale::EDemoOption>(option);
            if (skipped.Contains(id))
            {
                continue;
            }
            if (SOLNpcScale::GetOption(a, id) != SOLNpcScale::GetOption(b, id))
            {
                return false;
            }
        }
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoTabTest, "SOLTest.MenuState.DemoTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Demo tab sits between Settings and About and is selected like any other tab
bool FSOLMenuStateDemoTabTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    TestEqual(TEXT("Demo follows Settings"), static_cast<int32>(EMenuTab::Demo),
        static_cast<int32>(EMenuTab::Settings) + 1);
    TestEqual(TEXT("About follows Demo"), static_cast<int32>(EMenuTab::About), static_cast<int32>(EMenuTab::Demo) + 1);

    const FMenuState paused = MenuStateTestPaused();
    const FMenuState pausedDemo = SelectTab(paused, EMenuTab::Demo);
    TestTrue(TEXT("Paused: Demo selected"), pausedDemo.Tab == EMenuTab::Demo);
    TestTrue(TEXT("Paused: mode unchanged"), pausedDemo.Mode == EMenuMode::Paused);
    TestFalse(TEXT("Paused: map stays closed"), pausedDemo.bMapOpen);

    const FMenuState mainDemo = SelectTab(Initial(), EMenuTab::Demo);
    TestTrue(TEXT("MainMenu: Demo selected"), mainDemo.Tab == EMenuTab::Demo);
    TestTrue(TEXT("MainMenu: mode unchanged"), mainDemo.Mode == EMenuMode::MainMenu);

    const FMenuState playing = MenuStateTestPlaying();
    TestTrue(TEXT("Playing: Demo selection ignored"), MenuStateTestEqual(SelectTab(playing, EMenuTab::Demo), playing));

    // Leaving the pause menu from the Demo tab and pausing again lands on Resume
    FMenuState state = Apply(pausedDemo, EMenuEvent::EscapePressed);
    TestTrue(TEXT("Escape from the Demo tab resumes"), state.Mode == EMenuMode::Playing);
    state = Apply(state, EMenuEvent::EscapePressed);
    TestTrue(TEXT("Paused again"), state.Mode == EMenuMode::Paused);
    TestTrue(TEXT("Tab reset to Resume"), state.Tab == EMenuTab::Resume);

    // Play from the main menu's Demo tab starts the game as usual
    TestTrue(TEXT("Play from the Demo tab"), Apply(mainDemo, EMenuEvent::PlayPressed).Mode == EMenuMode::Playing);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoRowsTest, "SOLTest.MenuState.DemoRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The page has one row per option followed by the combat model, seed and ramp rows (24 in all)
bool FSOLMenuStateDemoRowsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    TestEqual(TEXT("24 rows"), DemoRowCount(), 24);
    TestEqual(TEXT("options plus extras"), DemoRowCount(), MENUSTATE_OPTION_ROWS + MENUSTATE_EXTRA_ROWS);
    TestEqual(TEXT("three extra rows"), MENUSTATE_EXTRA_ROWS, 3);
    TestEqual(TEXT("combat model row"), DemoExtraRow(EDemoExtraRow::CombatModel), MENUSTATE_OPTION_ROWS);
    TestEqual(TEXT("seed row"), DemoExtraRow(EDemoExtraRow::Seed), MENUSTATE_OPTION_ROWS + 1);
    TestEqual(TEXT("ramp row"), DemoExtraRow(EDemoExtraRow::Ramp), MENUSTATE_OPTION_ROWS + 2);
    TestEqual(TEXT("ramp is the last row"), DemoExtraRow(EDemoExtraRow::Ramp), DemoRowCount() - 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoDefaultPageTest, "SOLTest.MenuState.DemoDefaultPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The default page holds the default options and seed, the hybrid combat model and no ramp
bool FSOLMenuStateDemoDefaultPageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FDemoPage page = DefaultDemoPage();
    FDemoPage expected;
    expected.Options = SOLNpcScale::DefaultOptions();
    expected.CombatModel = 2;
    expected.bRamp = false;
    TestTrue(TEXT("matches the contract's default page"), MenuStateTestPageEqual(page, expected));
    TestEqual(TEXT("combat model is hybrid"), page.CombatModel, 2);
    TestEqual(TEXT("combat model is the constant"), page.CombatModel, SOL::NPC_COMBAT_DEFAULT_MODEL);
    TestEqual(TEXT("constant is hybrid"), SOL::NPC_COMBAT_DEFAULT_MODEL, 2);
    TestEqual(TEXT("default seed"), page.Options.Seed, SOL::NPC_DEFAULT_SEED);
    TestFalse(TEXT("ramp off"), page.bRamp);
    TestEqual(TEXT("default NPC count"), page.Options.NpcCount, 1000000);
    TestTrue(TEXT("a default-constructed page matches"), MenuStateTestPageEqual(FDemoPage(), page));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoStepOptionRowsTest, "SOLTest.MenuState.DemoStepOptionRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stepping an option row equals SOLNpcScale::StepOption on that option and leaves the other fields alone
bool FSOLMenuStateDemoStepOptionRowsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    using SOLNpcScale::EDemoOption;
    const FDemoPage pages[] = { DefaultDemoPage(), MenuStateTestCustomPage() };
    const int32 directions[] = { -1, 1, -3, 5 };

    // Every option row, both pages and every direction delegate to StepOption
    for (const FDemoPage& page : pages)
    {
        for (int32 row = 0; row < MENUSTATE_OPTION_ROWS; ++row)
        {
            for (const int32 direction : directions)
            {
                FDemoPage expected = page;
                expected.Options = SOLNpcScale::StepOption(page.Options, static_cast<EDemoOption>(row), direction);
                const FDemoPage stepped = StepDemoRow(page, row, direction);
                TestTrue(FString::Printf(TEXT("row %d direction %d equals StepOption"), row, direction),
                    MenuStateTestPageEqual(stepped, expected));
                TestEqual(FString::Printf(TEXT("row %d keeps the combat model"), row), stepped.CombatModel,
                    page.CombatModel);
                TestEqual(FString::Printf(TEXT("row %d keeps the seed"), row), stepped.Options.Seed,
                    page.Options.Seed);
                TestEqual(FString::Printf(TEXT("row %d keeps the ramp flag"), row), stepped.bRamp, page.bRamp);
            }
        }
    }

    // NpcCount steps through the presets
    const int32 npcRow = MenuStateTestOptionRow(EDemoOption::NpcCount);
    const FDemoPage defaults = DefaultDemoPage();
    TestEqual(TEXT("1M down -> 500k"), StepDemoRow(defaults, npcRow, -1).Options.NpcCount, 500000);
    TestEqual(TEXT("1M up stays at the top preset"), StepDemoRow(defaults, npcRow, 1).Options.NpcCount, 1000000);
    const FDemoPage custom = MenuStateTestCustomPage();
    TestEqual(TEXT("10k up -> 100k"), StepDemoRow(custom, npcRow, 1).Options.NpcCount, 100000);
    TestEqual(TEXT("10k down -> 1k"), StepDemoRow(custom, npcRow, -1).Options.NpcCount, 1000);

    // A cross-option rule applies: stepping NpcCount below the battle ship count limits the battle ships
    FDemoPage small = DefaultDemoPage();
    small.Options = SOLNpcScale::SetOption(small.Options, EDemoOption::NpcCount, 1000.0);
    TestEqual(TEXT("battle ships limited to 1k"), small.Options.BattleShipCount, 1000);
    const FDemoPage empty = StepDemoRow(small, npcRow, -1);
    TestEqual(TEXT("1k down -> 0"), empty.Options.NpcCount, 0);
    TestEqual(TEXT("battle ships limited to 0"), empty.Options.BattleShipCount, 0);

    // A bool option toggles
    const int32 combatRow = MenuStateTestOptionRow(EDemoOption::NpcCombat);
    TestFalse(TEXT("NPC combat toggled off"), StepDemoRow(defaults, combatRow, 1).Options.bNpcCombat);
    TestTrue(TEXT("NPC combat toggled back on"), StepDemoRow(custom, combatRow, -1).Options.bNpcCombat);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoStepCombatModelTest, "SOLTest.MenuState.DemoStepCombatModel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The combat model row steps within [0, 2] without wrapping, clamping an out-of-range stored value first
bool FSOLMenuStateDemoStepCombatModelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const int32 row = MenuStateTestExtraRow(EDemoExtraRow::CombatModel);
    FDemoPage page = MenuStateTestCustomPage();

    // In-range values step by one and stop at the ends
    page.CombatModel = 0;
    TestEqual(TEXT("0 down stays 0"), StepDemoRow(page, row, -1).CombatModel, 0);
    TestEqual(TEXT("0 up -> 1"), StepDemoRow(page, row, 1).CombatModel, 1);
    page.CombatModel = 1;
    TestEqual(TEXT("1 down -> 0"), StepDemoRow(page, row, -1).CombatModel, 0);
    TestEqual(TEXT("1 up -> 2"), StepDemoRow(page, row, 1).CombatModel, 2);
    page.CombatModel = 2;
    TestEqual(TEXT("2 up stays 2 (no wrap)"), StepDemoRow(page, row, 1).CombatModel, 2);
    TestEqual(TEXT("2 down -> 1"), StepDemoRow(page, row, -1).CombatModel, 1);
    TestEqual(TEXT("only the sign of a large step counts"), StepDemoRow(page, row, -7).CombatModel, 1);

    // Out-of-range stored values are clamped before the step
    page.CombatModel = 9;
    TestEqual(TEXT("9 clamps to 2, down -> 1"), StepDemoRow(page, row, -1).CombatModel, 1);
    TestEqual(TEXT("9 clamps to 2, up stays 2"), StepDemoRow(page, row, 1).CombatModel, 2);
    page.CombatModel = -4;
    TestEqual(TEXT("-4 clamps to 0, up -> 1"), StepDemoRow(page, row, 1).CombatModel, 1);
    TestEqual(TEXT("-4 clamps to 0, down stays 0"), StepDemoRow(page, row, -1).CombatModel, 0);
    TestEqual(TEXT("direction 0 keeps even an out-of-range value"), StepDemoRow(page, row, 0).CombatModel, -4);

    // Nothing else moves
    page.CombatModel = 1;
    const FDemoPage stepped = StepDemoRow(page, row, 1);
    FDemoPage expected = page;
    expected.CombatModel = 2;
    TestTrue(TEXT("only the combat model changed"), MenuStateTestPageEqual(stepped, expected));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoStepSeedTest, "SOLTest.MenuState.DemoStepSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The seed row adds or subtracts one with uint32 wraparound
bool FSOLMenuStateDemoStepSeedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const int32 row = MenuStateTestExtraRow(EDemoExtraRow::Seed);
    FDemoPage page = MenuStateTestCustomPage();

    page.Options.Seed = 100u;
    TestEqual(TEXT("100 up -> 101"), StepDemoRow(page, row, 1).Options.Seed, 101u);
    TestEqual(TEXT("100 down -> 99"), StepDemoRow(page, row, -1).Options.Seed, 99u);
    TestEqual(TEXT("only the sign of a large step counts"), StepDemoRow(page, row, 10).Options.Seed, 101u);

    // Wraparound at both ends
    page.Options.Seed = 0u;
    TestEqual(TEXT("0 down wraps to 0xFFFFFFFF"), StepDemoRow(page, row, -1).Options.Seed, 0xFFFFFFFFu);
    TestEqual(TEXT("0 up -> 1"), StepDemoRow(page, row, 1).Options.Seed, 1u);
    page.Options.Seed = 0xFFFFFFFFu;
    TestEqual(TEXT("0xFFFFFFFF up wraps to 0"), StepDemoRow(page, row, 1).Options.Seed, 0u);
    TestEqual(TEXT("0xFFFFFFFF down -> 0xFFFFFFFE"), StepDemoRow(page, row, -1).Options.Seed, 0xFFFFFFFEu);

    TestEqual(TEXT("default seed up"), StepDemoRow(DefaultDemoPage(), row, 1).Options.Seed,
        SOL::NPC_DEFAULT_SEED + 1u);

    // Nothing else moves
    page.Options.Seed = 500u;
    FDemoPage expected = page;
    expected.Options.Seed = 501u;
    TestTrue(TEXT("only the seed changed"), MenuStateTestPageEqual(StepDemoRow(page, row, 1), expected));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoStepRampTest, "SOLTest.MenuState.DemoStepRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ramp row toggles in either direction and changes nothing else
bool FSOLMenuStateDemoStepRampTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const int32 row = MenuStateTestExtraRow(EDemoExtraRow::Ramp);
    const FDemoPage page = DefaultDemoPage();
    TestTrue(TEXT("off -> on (up)"), StepDemoRow(page, row, 1).bRamp);
    TestTrue(TEXT("off -> on (down)"), StepDemoRow(page, row, -1).bRamp);
    TestFalse(TEXT("toggled twice is off again"), StepDemoRow(StepDemoRow(page, row, 1), row, 1).bRamp);

    const FDemoPage custom = MenuStateTestCustomPage();
    TestFalse(TEXT("on -> off"), StepDemoRow(custom, row, 1).bRamp);
    FDemoPage expected = custom;
    expected.bRamp = false;
    TestTrue(TEXT("only the ramp flag changed"), MenuStateTestPageEqual(StepDemoRow(custom, row, -1), expected));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoStepIgnoredTest, "SOLTest.MenuState.DemoStepIgnored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Direction 0 on any row, and any step on a row outside the page, leave the page unchanged
bool FSOLMenuStateDemoStepIgnoredTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    const FDemoPage pages[] = { DefaultDemoPage(), MenuStateTestCustomPage() };
    for (const FDemoPage& page : pages)
    {
        for (int32 row = 0; row < DemoRowCount(); ++row)
        {
            TestTrue(FString::Printf(TEXT("row %d direction 0 unchanged"), row),
                MenuStateTestPageEqual(StepDemoRow(page, row, 0), page));
        }
        const int32 badRows[] = { -1, -100, DemoRowCount(), DemoRowCount() + 1, 1000 };
        for (const int32 row : badRows)
        {
            TestTrue(FString::Printf(TEXT("row %d up unchanged"), row),
                MenuStateTestPageEqual(StepDemoRow(page, row, 1), page));
            TestTrue(FString::Printf(TEXT("row %d down unchanged"), row),
                MenuStateTestPageEqual(StepDemoRow(page, row, -1), page));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoPresetsTest, "SOLTest.MenuState.DemoPresets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each preset sets exactly its fields through SetOption, in order, and leaves the seed, combat model and other options
bool FSOLMenuStateDemoPresetsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    using SOLNpcScale::EDemoOption;
    const FDemoPage page = MenuStateTestCustomPage();

    // Showcase: 250k ships, 2,000 in battles, stats on, no ramp
    const FDemoPage showcase = ApplyDemoPreset(page, EDemoPreset::Showcase);
    FDemoPage expected = page;
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::NpcCount, 250000.0);
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::BattleShipCount, 2000.0);
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::ShowStats, 1.0);
    expected.bRamp = false;
    TestTrue(TEXT("Showcase equals the SetOption chain"), MenuStateTestPageEqual(showcase, expected));
    TestEqual(TEXT("Showcase NPC count"), showcase.Options.NpcCount, 250000);
    TestEqual(TEXT("Showcase battle ships"), showcase.Options.BattleShipCount, 2000);
    TestTrue(TEXT("Showcase stats on"), showcase.Options.bShowStats);
    TestFalse(TEXT("Showcase ramp off"), showcase.bRamp);
    TestEqual(TEXT("Showcase keeps the seed"), showcase.Options.Seed, 777u);
    TestEqual(TEXT("Showcase keeps the combat model"), showcase.CombatModel, 1);
    const EDemoOption showcaseSet[] = { EDemoOption::NpcCount, EDemoOption::BattleShipCount, EDemoOption::ShowStats };
    TestTrue(TEXT("Showcase keeps the other options"),
        MenuStateTestOtherOptionsEqual(showcase.Options, page.Options, showcaseSet));
    TestFalse(TEXT("Showcase keeps NPC combat off"), showcase.Options.bNpcCombat);

    // Benchmark: 1M ships, stats on, no ramp, battle ships untouched
    const FDemoPage benchmark = ApplyDemoPreset(page, EDemoPreset::Benchmark);
    expected = page;
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::NpcCount, 1000000.0);
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::ShowStats, 1.0);
    expected.bRamp = false;
    TestTrue(TEXT("Benchmark equals the SetOption chain"), MenuStateTestPageEqual(benchmark, expected));
    TestEqual(TEXT("Benchmark NPC count"), benchmark.Options.NpcCount, 1000000);
    TestTrue(TEXT("Benchmark stats on"), benchmark.Options.bShowStats);
    TestFalse(TEXT("Benchmark ramp off"), benchmark.bRamp);
    TestEqual(TEXT("Benchmark keeps the seed"), benchmark.Options.Seed, 777u);
    TestEqual(TEXT("Benchmark keeps the combat model"), benchmark.CombatModel, 1);
    const EDemoOption countAndStats[] = { EDemoOption::NpcCount, EDemoOption::ShowStats };
    TestTrue(TEXT("Benchmark keeps the other options"),
        MenuStateTestOtherOptionsEqual(benchmark.Options, page.Options, countAndStats));

    // Ramp: 1M ships, stats on, ramp on (from a page with the ramp off)
    FDemoPage rampOff = page;
    rampOff.bRamp = false;
    const FDemoPage ramp = ApplyDemoPreset(rampOff, EDemoPreset::Ramp);
    expected = rampOff;
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::NpcCount, 1000000.0);
    expected.Options = SOLNpcScale::SetOption(expected.Options, EDemoOption::ShowStats, 1.0);
    expected.bRamp = true;
    TestTrue(TEXT("Ramp equals the SetOption chain"), MenuStateTestPageEqual(ramp, expected));
    TestEqual(TEXT("Ramp NPC count"), ramp.Options.NpcCount, 1000000);
    TestTrue(TEXT("Ramp stats on"), ramp.Options.bShowStats);
    TestTrue(TEXT("Ramp ramp on"), ramp.bRamp);
    TestEqual(TEXT("Ramp keeps the seed"), ramp.Options.Seed, 777u);
    TestEqual(TEXT("Ramp keeps the combat model"), ramp.CombatModel, 1);
    TestTrue(TEXT("Ramp keeps the other options"),
        MenuStateTestOtherOptionsEqual(ramp.Options, rampOff.Options, countAndStats));

    // Invalid presets change nothing
    TestTrue(TEXT("Count preset unchanged"), MenuStateTestPageEqual(ApplyDemoPreset(page, EDemoPreset::Count), page));
    TestTrue(TEXT("Garbage preset unchanged"),
        MenuStateTestPageEqual(ApplyDemoPreset(page, static_cast<EDemoPreset>(200)), page));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoPresetOrderTest, "SOLTest.MenuState.DemoPresetOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Showcase sets NpcCount before BattleShipCount, so a page with no ships still ends with 2,000 battle ships
bool FSOLMenuStateDemoPresetOrderTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    FDemoPage page = DefaultDemoPage();
    page.Options = SOLNpcScale::SetOption(page.Options, SOLNpcScale::EDemoOption::NpcCount, 0.0);
    TestEqual(TEXT("no ships"), page.Options.NpcCount, 0);
    TestEqual(TEXT("no battle ships"), page.Options.BattleShipCount, 0);

    const FDemoPage showcase = ApplyDemoPreset(page, EDemoPreset::Showcase);
    TestEqual(TEXT("Showcase NPC count"), showcase.Options.NpcCount, 250000);
    TestEqual(TEXT("Showcase battle ships survive the order"), showcase.Options.BattleShipCount, 2000);
    TestEqual(TEXT("Showcase keeps the default seed"), showcase.Options.Seed, SOL::NPC_DEFAULT_SEED);
    TestEqual(TEXT("Showcase keeps the default combat model"), showcase.CombatModel, SOL::NPC_COMBAT_DEFAULT_MODEL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoFormatDefaultsTest, "SOLTest.MenuState.DemoFormatDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every row of the default page prints in its kind's format
bool FSOLMenuStateDemoFormatDefaultsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    using SOLNpcScale::EDemoOption;
    const FDemoPage page = DefaultDemoPage();
    struct FExpectedRow
    {
        EDemoOption Option;
        const TCHAR* Text;
    };
    const FExpectedRow rows[] = {
        { EDemoOption::NpcCount, TEXT("1,000,000") },
        { EDemoOption::LaneFraction, TEXT("0.40") },
        { EDemoOption::BattleShipCount, TEXT("2,000") },
        { EDemoOption::BattleCount, TEXT("4") },
        { EDemoOption::BattleRadiusM, TEXT("3.0 km") },
        { EDemoOption::BattleMinDistanceM, TEXT("5.0 km") },
        { EDemoOption::BattleMaxDistanceM, TEXT("20 km") },
        { EDemoOption::FullAiRadiusM, TEXT("50 km") },
        { EDemoOption::MidAiRadiusM, TEXT("2,000 km") },
        { EDemoOption::FullAiBudget, TEXT("4,000") },
        { EDemoOption::MidAiBudget, TEXT("100,000") },
        { EDemoOption::AiHysteresis, TEXT("0.10") },
        { EDemoOption::MeshRadiusM, TEXT("2.0 km") },
        { EDemoOption::InstancedRadiusM, TEXT("200 km") },
        { EDemoOption::MeshBudget, TEXT("32") },
        { EDemoOption::InstancedBudget, TEXT("20,000") },
        { EDemoOption::RenderHysteresis, TEXT("0.10") },
        { EDemoOption::NpcCombat, TEXT("On") },
        { EDemoOption::ShowStats, TEXT("On") },
        { EDemoOption::RampSettleS, TEXT("5 s") },
        { EDemoOption::RampMeasureS, TEXT("10 s") },
    };
    TestEqual(TEXT("one expectation per option"), static_cast<int32>(UE_ARRAY_COUNT(rows)), MENUSTATE_OPTION_ROWS);
    for (const FExpectedRow& row : rows)
    {
        const int32 index = MenuStateTestOptionRow(row.Option);
        TestEqual(FString::Printf(TEXT("row %d"), index), FormatDemoValue(page, index), FString(row.Text));
    }
    TestEqual(TEXT("combat model"), FormatDemoValue(page, DemoExtraRow(EDemoExtraRow::CombatModel)),
        FString(TEXT("Hybrid")));
    TestEqual(TEXT("seed"), FormatDemoValue(page, DemoExtraRow(EDemoExtraRow::Seed)),
        FString::Printf(TEXT("%u"), SOL::NPC_DEFAULT_SEED));
    TestEqual(TEXT("ramp"), FormatDemoValue(page, DemoExtraRow(EDemoExtraRow::Ramp)), FString(TEXT("Off")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuStateDemoFormatValuesTest, "SOLTest.MenuState.DemoFormatValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Distances switch from one decimal to grouped whole km at 10 km; bools, models, seeds and bad rows format per contract
bool FSOLMenuStateDemoFormatValuesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLMenuState;
    using SOLNpcScale::EDemoOption;
    FDemoPage page = DefaultDemoPage();
    const int32 radiusRow = MenuStateTestOptionRow(EDemoOption::BattleRadiusM);

    // Distances (the fields are set directly: formatting does not sanitize)
    page.Options.BattleRadiusM = 3000.0;
    TestEqual(TEXT("3000 m"), FormatDemoValue(page, radiusRow), FString(TEXT("3.0 km")));
    page.Options.BattleRadiusM = 500.0;
    TestEqual(TEXT("500 m"), FormatDemoValue(page, radiusRow), FString(TEXT("0.5 km")));
    page.Options.BattleRadiusM = 9500.0;
    TestEqual(TEXT("9500 m"), FormatDemoValue(page, radiusRow), FString(TEXT("9.5 km")));
    page.Options.BattleRadiusM = 10000.0;
    TestEqual(TEXT("10000 m"), FormatDemoValue(page, radiusRow), FString(TEXT("10 km")));
    page.Options.BattleRadiusM = 20000.0;
    TestEqual(TEXT("20000 m"), FormatDemoValue(page, radiusRow), FString(TEXT("20 km")));
    page.Options.BattleRadiusM = 12600.0;
    TestEqual(TEXT("12600 m rounds"), FormatDemoValue(page, radiusRow), FString(TEXT("13 km")));
    page.Options.MidAiRadiusM = 2000000.0;
    TestEqual(TEXT("2,000,000 m"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::MidAiRadiusM)),
        FString(TEXT("2,000 km")));
    page.Options.FullAiRadiusM = 1234000.0;
    TestEqual(TEXT("1,234,000 m"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::FullAiRadiusM)),
        FString(TEXT("1,234 km")));

    // Integers group with commas
    page.Options.NpcCount = 0;
    TestEqual(TEXT("0 ships"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::NpcCount)),
        FString(TEXT("0")));
    page.Options.NpcCount = 250000;
    TestEqual(TEXT("250k ships"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::NpcCount)),
        FString(TEXT("250,000")));
    page.Options.MeshBudget = 999;
    TestEqual(TEXT("999 meshes"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::MeshBudget)),
        FString(TEXT("999")));

    // Fractions with two decimals, seconds with none
    page.Options.LaneFraction = 0.333;
    TestEqual(TEXT("lane fraction"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::LaneFraction)),
        FString(TEXT("0.33")));
    page.Options.RenderHysteresis = 0.25;
    TestEqual(TEXT("render hysteresis"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::RenderHysteresis)),
        FString(TEXT("0.25")));
    page.Options.RampSettleS = 2.4;
    TestEqual(TEXT("settle seconds"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::RampSettleS)),
        FString(TEXT("2 s")));
    page.Options.RampMeasureS = 30.0;
    TestEqual(TEXT("measure seconds"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::RampMeasureS)),
        FString(TEXT("30 s")));

    // Bools
    page.Options.bNpcCombat = false;
    TestEqual(TEXT("combat off"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::NpcCombat)),
        FString(TEXT("Off")));
    page.Options.bShowStats = false;
    TestEqual(TEXT("stats off"), FormatDemoValue(page, MenuStateTestOptionRow(EDemoOption::ShowStats)),
        FString(TEXT("Off")));
    page.bRamp = true;
    TestEqual(TEXT("ramp on"), FormatDemoValue(page, DemoExtraRow(EDemoExtraRow::Ramp)), FString(TEXT("On")));

    // Combat model names, out-of-range values reading as Bolts
    const int32 modelRow = DemoExtraRow(EDemoExtraRow::CombatModel);
    const TCHAR* modelNames[] = { TEXT("Bolts"), TEXT("Statistical"), TEXT("Hybrid") };
    for (int32 model = 0; model < 3; ++model)
    {
        page.CombatModel = model;
        TestEqual(FString::Printf(TEXT("model %d"), model), FormatDemoValue(page, modelRow),
            FString(modelNames[model]));
    }
    page.CombatModel = 7;
    TestEqual(TEXT("model 7 reads as Bolts"), FormatDemoValue(page, modelRow), FString(TEXT("Bolts")));
    page.CombatModel = -1;
    TestEqual(TEXT("model -1 reads as Bolts"), FormatDemoValue(page, modelRow), FString(TEXT("Bolts")));

    // Seeds print unsigned and ungrouped
    const int32 seedRow = DemoExtraRow(EDemoExtraRow::Seed);
    page.Options.Seed = 0xFFFFFFFFu;
    TestEqual(TEXT("largest seed"), FormatDemoValue(page, seedRow), FString(TEXT("4294967295")));
    page.Options.Seed = 0u;
    TestEqual(TEXT("zero seed"), FormatDemoValue(page, seedRow), FString(TEXT("0")));
    page.Options.Seed = 1234567u;
    TestEqual(TEXT("seed not grouped"), FormatDemoValue(page, seedRow), FString(TEXT("1234567")));

    // Rows outside the page
    TestEqual(TEXT("row -1 empty"), FormatDemoValue(page, -1), FString());
    TestEqual(TEXT("row past the end empty"), FormatDemoValue(page, DemoRowCount()), FString());
    TestEqual(TEXT("row 1000 empty"), FormatDemoValue(page, 1000), FString());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
