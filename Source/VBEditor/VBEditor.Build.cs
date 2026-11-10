using UnrealBuildTool;

// General uncooked editor features; animation authoring remains in VBAnimationEditor.
public class VBEditor : ModuleRules
{
    public VBEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayTags", "VB" });
        // The M01 typed async node stays private to this module when it lands.
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "BlueprintGraph", "KismetCompiler", "RigVM" });
    }
}
