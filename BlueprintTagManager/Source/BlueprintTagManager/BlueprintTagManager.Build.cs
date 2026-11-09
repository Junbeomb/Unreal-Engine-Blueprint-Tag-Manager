using UnrealBuildTool;

public class BlueprintTagManager : ModuleRules
{
    public BlueprintTagManager(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Kismet", "Slate", "SlateCore",
            "ToolMenus", "InputCore", "AssetRegistry", "Projects"
        });
    }
}
