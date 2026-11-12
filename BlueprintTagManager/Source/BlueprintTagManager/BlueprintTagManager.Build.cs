using UnrealBuildTool;

public class BlueprintTagManager : ModuleRules
{
    public BlueprintTagManager(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Kismet", "Slate", "SlateCore",
            "ToolMenus", "InputCore", "AssetRegistry", "Projects"
        });
        if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 0)
        {
            PrivateDependencyModuleNames.Add("EditorStyle");
        }
    }
}
