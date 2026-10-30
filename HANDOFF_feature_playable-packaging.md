# HANDOFF feature/playable-packaging

Win64 Development package that cooks `L_Hub` and still has the CSV tables at runtime. Branched from staging `fa4b4c3`. Code commit `6c36327`.

## What changed

- `Config/DefaultGame.ini`: a packaging block at the end of the file.
- `Source/GFGame/GFGame.Build.cs`: stage `Data/CSV` as Non-UFS.
- `Tools/package_win64.ps1`: RunUAT BuildCookRun for Win64 Development.
- `Source/GFTests/Private/GFPackagingTests.cpp`: editor automation tests for the CSV paths and the staging list.

`FGFDataTables::CsvPath` is unchanged. Editor and packaged builds both read `FPaths::ProjectDir()/Data/CSV/<name>.csv`.

## Decision

Stage `Data/CSV` with `RuntimeDependencies.Add("$(ProjectDir)/Data/CSV/...", StagedFileType.NonUFS)` in `GFGame.Build.cs`.

UE 5.8 `UProjectPackagingSettings::DirectoriesToAlwaysStageAsNonUFS` ("Additional Non-Asset Directories To Copy") says those paths are relative to the project Content directory. The property meta is `RelativeToGameContentDir`. `Data/CSV` sits next to `Content`, so `../Data/CSV` is outside that contract. The stager joins the ini path onto the Content directory.

`$(ProjectDir)/Data/CSV/...` copies the folder to `<staged project>/Data/CSV`. In the Win64 archive that is `Windows/<Project>/Data/CSV`, and `FPaths::ProjectDir()` is that project folder (`Binaries/Win64` is two levels down). `FFileHelper::LoadFileToString` reads the loose files. The editor path stays `ProjectDir/Data/CSV`.

Also chosen:

- `bUseIoStore=True` (UE5 container files).
- `bShareMaterialShaderCode=True` (shader code stored once).
- `BuildConfiguration=PPBC_Development`, matching `-clientconfig=Development`.
- `+DirectoriesToAlwaysCook=(Path="/Game/Art")`. Every `/Game/...` string in `Data/CSV` is under `/Game/Art`, and the cooker cannot see those strings. A Primary Asset Label would need a `.uasset`.
- `+MapsToCook=(FilePath="/Game/Maps/L_Hub")`. `GameDefaultMap` and `EditorStartupMap` in `Config/DefaultEngine.ini` are already `/Game/Maps/L_Hub.L_Hub`, so the package boots into the hub once that map is cooked.

Alternative: copy the CSVs under `Content/` and point `DirectoriesToAlwaysStageAsNonUFS` at that copy. That duplicates the source. `Data/CSV` stays the only copy.

This choice still needs a GitHub issue titled `Decision: stage Data/CSV with RuntimeDependencies NonUFS`, body starting with `Label: design-decision`, linked from work issue #85. This run's GitHub credentials can read issues and cannot create issues or comments (`Resource not accessible by integration`, HTTP 403).

## Config keys

`[/Script/UnrealEd.ProjectPackagingSettings]` in `Config/DefaultGame.ini`:

| Key | Value |
|---|---|
| `BuildConfiguration` | `PPBC_Development` |
| `bUseIoStore` | `True` |
| `bShareMaterialShaderCode` | `True` |
| `MapsToCook` | `/Game/Maps/L_Hub` |
| `DirectoriesToAlwaysCook` | `/Game/Art` |

`Source/GFGame/GFGame.Build.cs`:

```csharp
RuntimeDependencies.Add("$(ProjectDir)/Data/CSV/...", StagedFileType.NonUFS);
```

The `...` entry stages every file in the folder, including a CSV added later.

## Target sanity

No target or plugin change.

- `GFGame.Target.cs` (Game) does not list `GFTests`. `GFGameEditor.Target.cs` does.
- `GFGame.uproject` marks `GFTests` as `Type: Editor`. `GFTests.Build.cs` depends on `UnrealEd`, so a game target must not compile it. It is already excluded.
- `FunctionalTestingEditor`, `PythonScriptPlugin`, `ModelContextProtocol`, and `AllToolsets` use `TargetAllowList: ["Editor"]`.
- `VoxelFinishEditor` and `VoxelFinishTests` are `Type: Editor` in `VoxelFinish.uplugin`. The runtime `VoxelFinish` module stays in the game.

## Tests

Editor automation (`EditorContext | EngineFilter`), in `GFTests`:

- `GFGame.Packaging.CsvPaths`: scan `Source/` and `Plugins/` for `FGFDataTables::` calls, require the anchor tables (`DT_Blocks`, `DT_Recipes`, `DT_DinoDiet`, `DT_AssetMap`, `DT_AnimMap`, `DT_GiantDinoLines`, `DT_Emotes`), and require every loaded name to exist at `FGFDataTables::CsvPath()` under `Data/CSV`.
- `GFGame.Packaging.CsvStaging`: the packaging keys above are present, and every `Data/CSV/*.csv` is covered by the `$(ProjectDir)/Data/CSV/...` Non-UFS dependency (or by an explicit `Data/CSV/<name>.csv` entry, or by an ini `../Data/CSV` stage path).

A source scan on this tree found the same 19 names as `Data/CSV/*.csv`, with nothing loaded that is missing a file and no file the code does not load. Every `/Game/` string in those CSVs is under `/Game/Art`. Unreal was not built or cooked here.

Run on the dev PC with the other editor tests:

`UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Packaging;Quit" -unattended -nullrhi`

## Dev PC steps

1. From the project root:

   `powershell -ExecutionPolicy Bypass -File Tools/package_win64.ps1 -EngineDir <UE 5.8 root> -OutputDir <folder>`

   `-EngineDir` defaults to `UE_ENGINE_DIR`. The root is the folder that contains `Engine\`. A path that already ends in `Engine\` is accepted. The script prints the packaged exe (preferring `Binaries\Win64`) and, when that exe is under `Binaries\Win64`, the staged `Data\CSV` path and file count.

2. Boot that exe. The log should have no `GFDataTables: no asset and no CSV` warnings. The script's CSV line should show the 19 files.

3. This checkout has no `Content/Data/DT_*.uasset` (nothing under `Content/Data`). If the dev PC has `/Game/Data/DT_*.uasset` locally and they are not in git, either commit them or delete them so `Data/CSV` stays the only source. Log that outcome on #85.

## Follow-ups

- File the decision issue above and link it from #85. Progress comments for `6c36327` and the handoff commit could not be posted from this run.
- If the booted exe still logs a missing CSV, change only `FGFDataTables::CsvPath` for packaged builds and keep the editor path the same. Confirm the files landed at `<archive>/Windows/<Project>/Data/CSV`.
- `/Game/Art` is cooked in full, so the package includes art nothing in the hub references yet.
