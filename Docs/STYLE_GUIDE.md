<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SOLTest — C++ Code Style Guide (DRAFT)

How C++ code in this repository should **look and be structured**: file headers,
formatting, naming, includes, comments, class layout, Unreal-specific rules, and
the data-oriented rules this project needs to reach ~1M simulated entities.

The house style is derived from the majority conventions of the sibling engine
**TrueReality** (`acidrainstudios/TrueReality`, branch `master`, 309 first-party
`.h`/`.cpp` files surveyed under `include/`, `src/`, `Examples/`, `Tests/`).
Where TrueReality's style conflicts with something Unreal Engine 5.8 requires or
strongly expects, the conflict is called out in a **Review needed** note with a
proposed UE-compatible resolution. A few rules were decided by the user and
override TrueReality where they differ (file banner text, function comments,
centralized asset paths).

The evidence counts behind each rule are summarized in the
[Appendix](#appendix--evidence-from-truereality).

> **On existing code that breaks these rules:** this guide describes the *target*
> shape. The files generated from Epic's template (`SOLTest.h/.cpp`, `*.Build.cs`,
> `*.Target.cs`) and the early `Source/SOLTest/Universe/` headers use Epic's
> default style (tabs, `// Copyright Epic Games` banner, PascalCase constants).
> Treat those as debt to fix when you next touch the file; new and refactored code
> follows this guide.

---

## 1. File header

Every `.h`, `.cpp`, `.cs` (Build/Target rules) and `.usf`/`.ush` file opens with
the company banner — nothing above it. The shape is TrueReality's `/* ... */`
block banner (used by 309/309 files), with the project text:

```cpp
/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once
```

Markdown files under `Docs/` use the one-line HTML-comment form instead:

```markdown
<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
```

- Replace Epic's `// Copyright Epic Games, Inc. All Rights Reserved.` line in any
  template-generated file you touch.
- Save files as **UTF-8** so the `©` survives. (262 TrueReality files store it
  as a legacy Windows-1252 byte, which shows as mojibake; don't copy that.)

> **Review needed:** TrueReality's banner also carries the LGPL notice and a
> `* @author <Name>` line (305/309 files). The LGPL text does not apply here and is
> dropped. Decide whether to keep an optional `* @author` line after the copyright
> line; this draft omits it.

---

## 2. Indentation & whitespace

- **4 spaces per indent level. No tabs.** (TrueReality: 41,855 space-indented
  lines vs 5 tab-indented lines; 1 file of 309 contains any tab.)
- Namespace contents **are** indented one level (TrueReality indents everything
  inside `namespace trXxx { ... }`).
- One statement per line. No trailing whitespace; end files with one newline.
- **Line length: soft limit of 120 columns.** TrueReality has no hard limit, but
  97.6% of lines are ≤ 100 columns and 99.2% are ≤ 120. Break long parameter lists
  and Doxygen prose before 120.
- One blank line between function definitions; a blank line after each access
  specifier (`public:` + blank line, ~70% of TrueReality classes).
- A space after control keywords and around binary operators: `if (x)`,
  `for (int32 index = 0; index < count; ++index)` (597 of 598 `if` statements).

> **Review needed:** Epic's coding standard and the UE project templates use
> **tabs** (the existing SOLTest files are tab-indented). Tabs are a convention,
> not a UHT/UBT requirement, so this draft keeps TrueReality's 4 spaces. If
> adopted, add a root `.editorconfig` (`indent_style = space`, `indent_size = 4`)
> so Rider/Visual Studio don't re-insert tabs, and convert template files on touch.

---

## 3. Braces & statements

- **Allman braces everywhere** — the opening brace goes on its own line for
  namespaces, classes, functions, and control statements (TrueReality: 3,146
  own-line `{` vs 2 end-of-line `{`; zero `} else` on one line).
- **Always brace** `if`/`else`/`for`/`while` bodies, even single statements
  (418 of 425 `if` statements are braced).
- Trivial inline accessors in headers may stay on one line:
  `float GetMass() const { return mMass; }`.
- Empty bodies are `{` and `}` on separate lines, or `{}` for a trivial override.
- Constructor initializer lists start on the next line, indented, with a leading
  `:`; one member per line, commas trailing:

```cpp
    FSOLOrbitSolver::FSOLOrbitSolver(const double gravParam)
        : mGravParam(gravParam),
          mIterationLimit(DEFAULT_ITERATION_LIMIT)
    {
    }
```

```cpp
    if (body != nullptr)
    {
        body->Advance(deltaSeconds);
    }
    else
    {
        UE_LOG(LogSOL, Warning, TEXT("Advance called with no body"));
    }
```

---

## 4. Includes

- Headers use **`#pragma once`** (152/154 TrueReality headers; no include guards).
  This is also what UE expects.
- In a `.cpp`, the **first include is the file's own header** (141/155 files),
  followed by a blank line.
- Then group, with a blank line between groups, in this order (the dominant
  TrueReality order is project → third-party → standard library):
  1. Own header (`.cpp` only)
  2. Project (SOLTest) headers
  3. Engine / plugin headers
  4. Standard library headers (rare in UE code)
- In a UE header, `#include "Foo.generated.h"` is **always the last include**
  (UHT requirement), after `CoreMinimal.h` and any other includes.
- Include what you use (IWYU); forward-declare in headers where a pointer or
  reference suffices.

> **Review needed:** TrueReality includes project headers with angle brackets and
> a module-root path (`#include <trManager/ActorBase.h>`; 1,192 angle vs 126 quoted
> includes, quotes reserved for same-folder headers like `"Export.h"`). UE code
> conventionally uses **quotes for all project and engine headers**
> (`#include "CoreMinimal.h"`), and `.generated.h` must be quoted. Proposed: keep
> TrueReality's *module-root-relative path* idea (`#include "Universe/SOLTypes.h"`,
> since `Source/SOLTest/` is on the module include path), but use **quotes** for
> project and engine headers and angle brackets only for standard-library headers.

```cpp
/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLKeplerSystem.h"

#include "Universe/SOLConstants.h"
#include "Universe/SOLTypes.h"

#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
```

---

## 5. Naming

| Kind | Convention | Example |
|------|-----------|---------|
| UObject class | `U` + `SOL` + PascalCase | `USOLSimClockSubsystem` |
| Actor class | `A` + `SOL` + PascalCase | `ASOLShipPawn` |
| Struct / plain class | `F` + `SOL` + PascalCase | `FSOLState`, `FSOLOrbitElements` |
| Mass fragment / tag | `F` + `SOL` + PascalCase + `Fragment`/`Tag` | `FSOLOrbitFragment`, `FSOLPlanetTag` |
| Interface | `U`/`I` + `SOL` + PascalCase | `USOLDockable` / `ISOLDockable` |
| Enum | `E` + `SOL` + PascalCase, `enum class` | `ESOLFlightMode` |
| Template class | `T` + PascalCase | `TSOLRingBuffer` |
| Type alias | `using`, UE-prefixed PascalCase | `using FSOLPosition = FVector3d;` |
| Function / method | PascalCase, verb-first | `AdvanceOrbit`, `RegisterBody` |
| Accessors | `Get` / `Set` / `Is` / `Has` + PascalCase | `GetMass`, `SetTimeScale`, `IsPaused` |
| Non-reflected member (class) | `m` prefix + PascalCase | `mBodyIndex`, `mIsPaused` |
| `UPROPERTY` member | PascalCase, `b` prefix for bools | `MaxThrust`, `bFlightAssist` |
| Public field of a plain data struct | PascalCase | `Position`, `SemiMajorAxis` |
| Parameter / local variable | camelCase | `deltaSeconds`, `bodyIndex` |
| Constant (`static constexpr`, namespace constant) | UPPER_SNAKE_CASE | `SECONDS_PER_DAY`, `MAX_BODIES` |
| Enum value | PascalCase | `ESOLFlightMode::Newtonian` |
| Macro | `SOL_` + UPPER_SNAKE_CASE | `SOL_LOG_ORBIT`, `SOLTEST_API` (generated) |
| Namespace (non-reflected helpers) | `SOL`, nested PascalCase | `SOL::Kepler` |
| File | exactly the type name without its UE prefix | `SOLShipPawn.h` for `ASOLShipPawn` |

Rules:

- **One primary type per file**, file named after it (TrueReality: one class per
  `.h`/`.cpp` pair, file name = class name, 322/326 classes PascalCase). UE's file
  name drops the `U`/`A`/`F` prefix.
- **`SOL` infix** on every project type so SOLTest types never collide with engine
  or plugin types and are easy to grep. (The existing `FSOLState` already does this.)
- **Constants are UPPER_SNAKE_CASE** (TrueReality: 172/173 `static const` members,
  e.g. `CLASS_TYPE`, `MESSAGE_TYPE`). Prefer `static constexpr` / `inline constexpr`.
- **Names are never bare literals at the call site** — name magic numbers and
  string IDs as constants (see §7).
- Non-reflected `bool` members keep TrueReality's `mIs…`/`mHas…` form
  (`mIsRunning`, `mIsRegistered`); reflected bools use Epic's `b` prefix.

> **Review needed — type prefixes.** TrueReality types have no prefix (`ActorBase`,
> `SmrtPtr`, `SystemManager`). UHT **requires** `U`/`A`/`F`/`E`/`I`/`T`/`S` prefixes
> for reflected types and the engine expects them everywhere. Proposed: Epic prefix
> + `SOL` infix + TrueReality-style PascalCase name, as in the table.

> **Review needed — member variables.** TrueReality prefixes members with `m`
> (218 `mPascal` member declarations vs 11 other prefixed/suffixed forms; plain
> data structs such as `TimingStructure` use bare camelCase fields). UE
> strongly expects reflected members to be PascalCase with `b` for bools: the
> editor derives display names from them (`mMaxThrust` shows as "M Max Thrust";
> `bFlightAssist` shows as "Flight Assist"), Blueprint pins use them, and inherited
> engine members (`RootComponent`, `PrimaryActorTick`) are PascalCase. Proposed
> split: **`UPROPERTY` members and public struct fields follow Epic** (PascalCase,
> `b` bools); **non-reflected private/protected class members keep TrueReality's
> `m` prefix**. Alternative: drop `m` entirely and follow Epic for all members.

> **Review needed — parameters and locals.** TrueReality uses camelCase (1,676/1,687
> header parameters; 264/266 initialized locals). Epic uses PascalCase. Not
> required by UHT, so camelCase is kept; note that `UFUNCTION` parameter names
> become Blueprint pin labels (`deltaSeconds` → "Delta Seconds", which is fine).

> **Review needed — constants in existing code.** `Universe/SOLTypes.h` currently
> uses Epic-style PascalCase constants (`SOL::SecondsPerDay`). Under this guide they
> become `SOL::SECONDS_PER_DAY`. Confirm before renaming.

> **Review needed — namespaces.** TrueReality uses one lowerCamel namespace per
> module (`trUtil`, `trBase`, `trCore`, `trManager`). UHT does **not** support
> reflected types (`UCLASS`/`USTRUCT`/`UENUM`) inside namespaces, so those stay at
> global scope with the `SOL` infix. Proposed: non-reflected helpers, math and
> constants go in `namespace SOL` (already used by the existing code), nested
> PascalCase sub-namespaces when useful (`SOL::Kepler`). A TrueReality-literal
> alternative would be `solUniverse`, `solFlight`, etc.

---

## 6. Types, pointers & modern C++

- **Pointer/reference binds to the type:** `FSOLState& state`, `UWorld* world`
  (TrueReality: `Type& name` in 141 files vs `Type &name` in 25; `Type* name` in
  ~78% of declarations).
- **West const:** `const FString& name` (2,051 vs 57 `Type const&`). Mark every
  non-mutating method `const`; pass non-trivial types by `const&`.
- **`nullptr`, never `NULL` or `0`** for pointers. (TrueReality: 90 `nullptr` vs
  68 `NULL`, the `NULL`s being in older ported code.) Compare explicitly:
  `if (ptr != nullptr)` is the TrueReality idiom; `if (ptr)` is also acceptable.
- **`using` aliases, not `typedef`** (462 vs 2).
- **`auto` sparingly** — TrueReality uses it only 6 times, all for range-for and
  iterators (`for (auto&& module : mActorModules)`). Allowed for iterators,
  range-for, lambdas and when the type is spelled on the same line
  (`auto* comp = CreateDefaultSubobject<USOLFoo>(...)`); otherwise spell the type.
  This matches Epic's guidance.
- **`override` on every override** (382 uses). TrueReality writes
  `virtual void OnTick(...) override;` — keep `virtual` + `override` together for
  consistency. Use `final` where a class or method must not be extended.
- **Base-class calls go through `Super::`**. See the note below.
- Prefer UE types: `int32`/`uint8`/`double`, `FString`/`FName`/`FText`,
  `TArray`/`TMap`/`TSet`. Use `double` for universe-scale positions
  (`FVector3d`).

> **Review needed — base-class alias.** TrueReality declares
> `using BaseClass = trManager::EntityBase;` in each class (58 classes) and calls
> `BaseClass::OnAddedToSysMan()`. For `UCLASS`/`USTRUCT` types `GENERATED_BODY()`
> already provides `Super` and `ThisClass`. Proposed: use **`Super::`** in all
> reflected types and do not add a `BaseClass` alias; plain non-reflected
> hierarchies may keep `using BaseClass = ...;`.

> **Review needed — ownership & smart pointers.** TrueReality uses its own
> intrusive `trBase::SmrtPtr<T>` (203 uses) over OSG ref-counting, plus raw `new`
> (214 uses) handed to smart pointers; `std::` smart pointers are almost unused.
> UE requires different rules: UObjects are created with
> `NewObject`/`CreateDefaultSubobject`/`SpawnActor`, never `new`/`delete`; UObject
> references held by UObjects are `UPROPERTY()` `TObjectPtr<T>` (so GC sees them),
> non-owning weak refs are `TWeakObjectPtr<T>`. Non-UObject heap data uses
> `TUniquePtr`/`TSharedPtr`/`TSharedRef` (`MakeUnique`/`MakeShared`). No
> `std::shared_ptr`/`std::unique_ptr` in gameplay code.

---

## 7. Asset & content paths live in one place

Never hardcode an asset/content path (`/Game/...` mesh, material, texture, Niagara
system, data table, sound) inside gameplay code — actors, components, subsystems
or Mass processors. Paths live in **one central, owned place**:

1. **Preferred — data assets / developer settings.** Expose asset references as
   `TSoftObjectPtr<>` / `TSoftClassPtr<>` properties on a `UPrimaryDataAsset`
   (e.g. `USOLBodyCatalog`) or a `UDeveloperSettings` subclass
   (`USOLProjectSettings`, `Config = Game`), and set them in the editor/`.ini`.
2. **When C++ must name a path** (e.g. a default for
   `ConstructorHelpers`/soft-path fallback), put it in **`Source/SOLTest/SOLConstants.h`**
   as an UPPER_SNAKE constant and reference it from there.

```cpp
// in SOLConstants.h
namespace SOL::Paths
{
    inline constexpr const TCHAR* DEFAULT_PLANET_MATERIAL = TEXT("/Game/SOL/Materials/M_Planet.M_Planet");
    inline constexpr const TCHAR* BODY_CATALOG = TEXT("/Game/SOL/Data/DA_BodyCatalog.DA_BodyCatalog");
}

// at the call site
const FSoftObjectPath materialPath(SOL::Paths::DEFAULT_PLANET_MATERIAL);
```

The same principle applies to other named literals that several files share
(input action names, gameplay tags, config section names): name them once.
Physical constants and unit conversions live alongside them (`SOLTypes.h` /
`SOLConstants.h`), never inline.

---

## 8. Comments

- **Every function — including file-local helpers and lambdas assigned to a
  name — gets a single-line `//` description directly above it.** Short prose,
  sentence case (TrueReality comments are sentence case: `//Make sure we are
  dealing with an Actor Module`).
- **Any non-trivial block longer than ~5 lines** inside a function gets a brief
  inline `//` comment above it. TrueReality does this consistently:
  `//Find an Actor Module with the same reference`, `//Clear the reattach list`.
- Write `// Comment` with a space after the slashes (TrueReality is split
  ~51/49: 697 lines with a space vs 668 without; the spaced form wins narrowly).
- Short trailing comments after member declarations are fine and common in
  TrueReality: `ActorModules mActorModules; // Actor Module storage`.
- Use `/*unused*/` for unused parameter names: `void OnTickRemote(const FSOLTick& /*tick*/)`.
- Don't leave commented-out code (`//#include <iomanip>`) in committed files.

```cpp
    //////////////////////////////////////////////////////////////////////////
    // Advances every body's mean anomaly by the scaled sim time
    void USOLKeplerSubsystem::AdvanceAll(const double deltaSimSeconds)
    {
        // Solve Kepler's equation for each body; positions are written back in place
        for (int32 index = 0; index < mBodies.Num(); ++index)
        {
            ...
        }
    }
```

> **Review needed — header Doxygen vs single-line comments.** TrueReality documents
> nearly every *declaration* in headers with a multi-line Doxygen block
> (`/** @fn ... @brief ... @param ... @return */`: 3,306 `/**` blocks, 1,719 `@fn`,
> 2,235 `@brief`, 2,335 `@param`) and puts only a separator line above *definitions*
> in `.cpp`. The user rule requires a single-line description on every function.
> UE additionally extracts **`/** */` comments above `UPROPERTY`/`UFUNCTION`/`UCLASS`
> as editor and Blueprint tooltips** — `//` comments are not extracted. Proposed:
> - `.cpp` definitions and all non-reflected helpers: single-line `//` (the rule).
> - Header declarations of reflected members and types: a single-line
>   `/** Description. */` (still one line, but becomes the tooltip).
> - Multi-line Doxygen (`@param`/`@return`) is optional, for public APIs that need
>   it; drop TrueReality's redundant `@fn <signature>` line.

---

## 9. Separator lines in `.cpp`

TrueReality places a **74-slash separator line** directly above every function
definition in `.cpp` files (1,329 of 1,580 separators are exactly 74 characters;
103/155 `.cpp` files, the rest being near-empty wrappers). Keep it, followed by
the §8 one-line description:

```cpp
    //////////////////////////////////////////////////////////////////////////
    // Registers a body with the Kepler registry and returns its index
    int32 USOLKeplerSubsystem::RegisterBody(const FSOLOrbitElements& elements)
    {
```

> **Review needed:** separator + description is two lines per function. If that
> feels heavy, drop the separator and keep only the `//` description line.

TrueReality has no in-class section banners; group class members by access level
(§10) instead.

---

## 10. Class layout

- Access order: **`public` → `protected` → `private`** (in TrueReality 145 classes
  open with `public:`, and `protected` precedes `private`).
- Within `public`: type aliases and constants first, then constructors/destructor,
  then methods (overrides grouped together), then (for UE) `UPROPERTY` members
  after the methods they relate to or in a trailing `public`/`protected` block.
- Private data members go **last** in the class (TrueReality keeps `mX` members at
  the bottom of the `private:` section).
- In reflected types, `GENERATED_BODY()` is the **first line of the body**,
  followed by a blank line and an explicit `public:`.

```cpp
/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "SOLShipPawn.generated.h"

class USOLFlightComponent;

/** Thin player pawn that bridges input and camera to the ship's Mass entity. */
UCLASS()
class SOLTEST_API ASOLShipPawn : public APawn
{
    GENERATED_BODY()

public:

    // Sets default subobjects
    ASOLShipPawn();

    // Binds Enhanced Input actions to flight controls
    virtual void SetupPlayerInputComponent(UInputComponent* inputComponent) override;

    /** Maximum forward thrust, in newtons. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|Flight", meta = (ClampMin = "0.0"))
    double MaxThrust = 1.0e6;

protected:

    // Registers the pawn with its Mass ship entity
    virtual void BeginPlay() override;

private:

    UPROPERTY(VisibleAnywhere, Category = "SOL|Flight")
    TObjectPtr<USOLFlightComponent> FlightComponent;

    FMassEntityHandle mShipEntity; // Mass entity this pawn proxies
};
```

---

## 11. Enums

- Always **`enum class`**, `E` + `SOL` prefix; reflected enums declare
  `: uint8`.
- One enumerator per line, trailing comma allowed.

> **Review needed:** TrueReality mostly uses plain (unscoped) enums (10 plain vs 3
> `enum class`) with **UPPER_SNAKE_CASE** values (`TRAVERSE_ALL_CHILDREN`,
> `FILE_NOT_FOUND`). UHT expects `UENUM(BlueprintType) enum class ESOLFoo : uint8`
> and the value name is shown in the editor/Blueprints, where Epic's convention is
> PascalCase. Proposed: `enum class` everywhere; **PascalCase values** for all
> enums so reflected and non-reflected enums look the same. Alternative: keep
> UPPER_SNAKE values and add `UMETA(DisplayName = "...")` on reflected ones.

```cpp
/** How the flight model integrates ship motion. */
UENUM(BlueprintType)
enum class ESOLFlightMode : uint8
{
    Assisted,
    Newtonian,
};
```

---

## 12. Templates

- Write `template <typename T>` with a space after `template` (205 vs 40) and
  `typename` for type parameters (TrueReality mixes `class T` and `typename T`;
  `typename` is the majority).
- Template parameter names are PascalCase (`T`, `ElementType`); template classes
  take the `T` prefix in UE (`TSOLRingBuffer`).
- Template definitions live in the header (or an `.inl` included from it).
- `USTRUCT`/`UCLASS` cannot be templates; keep templates to non-reflected helpers.

---

## 13. Error handling & logging

- TrueReality's pattern: functions that can fail **return `bool` success** (or
  `nullptr`), check preconditions up front, and **log the reason** with its
  `LOG_E`/`LOG_W` macros (165 uses) — keep that pattern.
- Log through the project category: `UE_LOG(LogSOL, Error, TEXT("..."))`
  (`LogSOL` is declared in `Universe/SOLTypes.h`). Add sub-categories
  (`LogSOLFlight`, `LogSOLMass`) as systems grow. Include the object name in the
  message, as TrueReality does (`"The entity " + GetName() + " is not registered"`).
- Use `check()` / `checkf()` for programmer errors that must never happen,
  `ensure()` / `ensureMsgf()` for recoverable "should not happen" cases, and
  `verify()` when the expression must run in shipping builds.
- No string building or logging above `Verbose` in hot paths (see §15).

> **Review needed — exceptions.** TrueReality throws in a few places (40 `throw`,
> 22 `try`, in `SystemManager.cpp` and `FileUtils.cpp`). UE builds with C++
> exceptions **disabled**. Proposed: no `throw`/`try`/`catch` in SOLTest; use
> bool/`TOptional`/`TValueOrError` returns plus `UE_LOG`, `check`, or `ensure`.

---

## 14. Unreal-specific rules

### 14.1 Reflection macros

- `UCLASS()`, `USTRUCT()`, `UENUM()`, `UINTERFACE()`, `UPROPERTY()`,
  `UFUNCTION()` each go on **their own line directly above** the declaration.
- **Specifier order:** visibility/edit flags → Blueprint access → other flags →
  `Category` → `meta` last:
  `UPROPERTY(EditAnywhere, BlueprintReadWrite, Transient, Category = "SOL|Flight", meta = (ClampMin = "0.0"))`.
- Spaces around `=` inside specifiers; `Category` always quoted and rooted at
  `SOL|` with `|`-separated subcategories.
- Every `UObject*` member held by a UObject is a `UPROPERTY()` `TObjectPtr<>`,
  even if not edited, so the GC tracks it.
- Only reflect what needs to be reflected. Simulation data that the editor and
  Blueprints never touch stays plain C++ (cheaper, and allowed to follow §5's
  non-reflected naming).
- Exported types use the generated `SOLTEST_API` macro (TrueReality's analogue is
  `TR_MANAGER_EXPORT`).

### 14.2 Module & file layout

- Game module: `Source/SOLTest/`. Organize by **feature folder**
  (`Universe/`, `Flight/`, `Mass/`, `UI/`), with each type's `.h` and `.cpp` side
  by side in the same folder (the current layout).
- Include project headers by module-root-relative path: `"Universe/SOLTypes.h"`.
- Module-wide headers at the root: `SOLTest.h` (module), `SOLConstants.h`
  (paths/named literals, §7).
- If a system is split into its own module or plugin later, use UE's
  `Public/` + `Private/` split. It maps directly onto TrueReality's
  `include/<module>/` + `src/<module>/` split.

### 14.3 `Build.cs` / `Target.cs`

- Same banner (§1) and formatting rules (4 spaces, Allman) as C++.
- List dependencies **one per line, alphabetized**. A dependency is `Public` only
  if it appears in a public header of the module; otherwise `Private`.
- Delete the template's commented-out boilerplate once dependencies are real.
- No hardcoded absolute paths.

```csharp
/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

using UnrealBuildTool;

public class SOLTest : ModuleRules
{
    public SOLTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "MassEntity",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "EnhancedInput",
            "InputCore",
        });
    }
}
```

### 14.4 Other UE rules

- Use `TEXT("...")` for every string literal passed to UE APIs.
- Use UE containers and strings, not the STL (`TArray` not `std::vector`, `FString`
  not `std::string`). TrueReality's STL usage does not carry over.
- Tick is opt-in: set `PrimaryActorTick.bCanEverTick = false` unless the class
  truly needs it.
- No deprecated APIs (see SDD 1): Enhanced Input, Mass, Niagara, Chaos.

---

## 15. Data-oriented / Mass guidance

SOLTest must scale toward ~1M simulated entities, so simulation code is
data-oriented first:

- **No Actor per datum.** Bodies, asteroids, ships and NPCs are Mass entities.
  Actors are thin presentation or input proxies (the player's pawn) only.
- **Data lives in Mass fragments** (`FSOLOrbitFragment`, `FSOLTransformFragment`):
  small, plain `USTRUCT`s of POD fields with no pointers to UObjects. Use tags
  (`FMassTag`) for flags, shared fragments for per-archetype constants.
- **Processors** iterate chunks with `ForEachEntityChunk` and read fragments via
  `GetFragmentView`/`GetMutableFragmentView`. Declare exact read/write access in
  `ConfigureQueries` so Mass can parallelize.
- **No heap allocation in `Tick`, processor `Execute`, or any per-entity loop**:
  no `new`, no growing `TArray`s, no `FString` formatting, no `TMap` inserts.
  Pre-size with `Reserve()` / `SetNumUninitialized()` or use
  `TArray<..., TInlineAllocator<N>>` / frame-scoped `FMemStack` allocations.
- Prefer **structs-of-arrays** (parallel `TArray`s or separate fragments) over
  arrays of large structs when a hot loop only reads a few fields.
- Hot-path math is `double` where positions are universe-scale; avoid virtual
  calls and UObject access inside per-entity loops.
- Wrap processors and heavy systems in `TRACE_CPUPROFILER_EVENT_SCOPE` /
  `QUICK_SCOPE_CYCLE_COUNTER` so Unreal Insights shows their cost.
- No `UE_LOG` above `Verbose` inside per-entity loops.

---

## Appendix — evidence from TrueReality

Counts are over 309 first-party files (154 `.h`, 155 `.cpp`) in `include/`,
`src/`, `Examples/`, `Tests/`, from a shallow clone of `master`.

| Convention | Majority | Evidence |
|---|---|---|
| Banner | `/* ... */` block, product + copyright + LGPL + `@author` | 309/309 files; `@author` 305/309 |
| Header guard | `#pragma once` | 152/154 headers; 0 `#ifndef` guards |
| Indentation | 4 spaces | 41,855 space-led lines vs 5 tab-led lines |
| Braces | Allman | 3,146 own-line `{` vs 2 end-of-line |
| Braced single-statement `if` | Always | 418/425 |
| Line length | No hard limit | 97.6% ≤ 100 cols, 99.2% ≤ 120 |
| Own header first in `.cpp` | Yes | 141/155 |
| Include quoting | `<module/File.h>` angle brackets | 1,192 angle vs 126 quoted |
| Member prefix | `m` + PascalCase | 218 `mX` vs 11 other prefixes/suffixes |
| Constants | UPPER_SNAKE_CASE | 172/173 `static const` names |
| Methods | PascalCase | 1,454 vs ~30 (mostly STL-compat) |
| Params / locals | camelCase | 1,676/1,687 params; 264/266 locals |
| Namespaces | lowerCamel module (`trUtil`) | 4 module namespaces, no `using namespace` |
| `Type& name` | Type-side | 141 files vs 25 |
| West const | `const T&` | 2,051 vs 57 |
| `using` vs `typedef` | `using` | 462 vs 2 |
| `nullptr` vs `NULL` | `nullptr` (split) | 90 vs 68 |
| `auto` | Rare | 6 uses |
| `override` | Used | 382 uses |
| Doxygen in headers | `/** @fn @brief @param */` | 3,306 blocks; 1,719 `@fn` |
| `.cpp` separator | 74 slashes above each definition | 1,329 of 1,580 separators; 103/155 files |
| `//` spacing | Split | 697 spaced vs 668 unspaced lines |
| Enums | Plain enum, UPPER_SNAKE values | 10 plain vs 3 `enum class` |
| Templates | `template <typename T>` | 205 vs 40 spacing |
| Errors | bool return + `LOG_E`/`LOG_W`; few exceptions | 165 log calls; 40 `throw` |
| Access order | public, protected, private | 145 classes open with `public:` |

TrueReality has no `.clang-format`, `.editorconfig`, or written contribution
style (`CONTRIBUTING.md` is "Coming Soon"), so these rules are inferred from
the code only.

---

## See also

- [`SDDs/1-solar-system-architecture.md`](SDDs/1-solar-system-architecture.md) —
  cross-cutting design decisions (Mass for everything, scale targets), and its
  roadmap plan in [`Plans/`](Plans/1-solar-system-architecture-plan.md).
- [`Research/UE_PERFORMANCE_GUIDELINES.md`](Research/UE_PERFORMANCE_GUIDELINES.md) —
  the performance checklist that the post-code review uses.
- [Epic C++ Coding Standard](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) —
  the reference for anything this guide doesn't cover.
- TrueReality (`github.com/acidrainstudios/TrueReality`) — the source of the house
  style.
