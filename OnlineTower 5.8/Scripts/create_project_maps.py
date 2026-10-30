"""Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.

Creates only missing maps; existing artist edits are never overwritten.
"""
import unreal

MAPS = [
    ("/Game/Maps/Frontend/L_MainMenu", "TowerFrontendGameMode", None),
    ("/Game/Maps/Hub/L_Hub", "TowerHubGameMode", "TowerOfficeEnvironment"),
    ("/Game/Maps/Trials/L_Dungeon", "TowerTrialGameMode", "TowerDungeonEnvironment"),
    ("/Game/Maps/Trials/L_Duel", "TowerTrialGameMode", "TowerDuelEnvironment"),
    ("/Game/Maps/Trials/L_FloorFinal", "TowerTrialGameMode", "TowerFinalEnvironment"),
]

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for path, mode_name, environment_name in MAPS:
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("TOWER_MAP_KEEP " + path)
        continue
    mode = unreal.load_class(None, "/Script/OnlineTower." + mode_name)
    environment = (unreal.load_class(None, "/Script/OnlineTower." + environment_name)
                   if environment_name else None)
    if not mode or (environment_name and not environment):
        raise RuntimeError("Build the project before creating " + path)
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        raise RuntimeError("Cannot create world " + path)
    world.get_world_settings().set_editor_property("default_game_mode", mode)
    if environment:
        actor = actors.spawn_actor_from_class(environment, unreal.Vector(0, 0, 10000))
        if not actor:
            raise RuntimeError("Cannot create environment " + path)
        actor.set_actor_label(environment_name)
        actor.set_folder_path("Environment")
        start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-700, 0, 10120))
        start.set_actor_label("EditorPreviewStart")
        start.set_folder_path("Gameplay/Spawns")
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
        raise RuntimeError("Cannot save " + path)
    unreal.log("TOWER_MAP_CREATED " + path)

for path, mode_name, environment_name in MAPS:
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    if not world:
        raise RuntimeError("Cannot reload " + path)
    mode = world.get_world_settings().get_editor_property("default_game_mode")
    if mode.get_name() != mode_name:
        raise RuntimeError("Incorrect game mode: " + path)
    environments = [a for a in actors.get_all_level_actors()
                    if a.get_class().get_name().endswith("Environment")]
    if len(environments) != (1 if environment_name else 0):
        raise RuntimeError("Incorrect environment count: " + path)
    if environment_name and environments[0].get_class().get_name() != environment_name:
        raise RuntimeError("Incorrect environment class: " + path)
    unreal.log("TOWER_MAP_VERIFIED " + path)
