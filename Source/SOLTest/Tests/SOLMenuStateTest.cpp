/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuState.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
