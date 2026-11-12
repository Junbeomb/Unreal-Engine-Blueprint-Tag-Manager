#include "BlueprintTagProject.h"
#include "Runtime/Launch/Resources/Version.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

TArray<FString> FBlueprintTagProject::GetContentRoots()
{
    TArray<FString> Roots = { TEXT("/Game") };
    for (const auto& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        if (Plugin->GetType() == EPluginType::Project && Plugin->CanContainContent())
        {
            FString Root = Plugin->GetMountedAssetPath();
            Root.RemoveFromEnd(TEXT("/"));
            if (!Root.IsEmpty()) { Roots.AddUnique(Root); }
        }
    }
    return Roots;
}

bool FBlueprintTagProject::IsInFolders(const FString& PackageName, const TArray<FString>& Folders)
{
    for (const FString& Folder : Folders)
    {
        if (PackageName == Folder || PackageName.StartsWith(Folder + TEXT("/"))) { return true; }
    }
    return false;
}

bool FBlueprintTagProject::Discover(const FString& Folder, TArray<FAssetData>& OutAssets, FText& OutError)
{
    OutAssets.Reset();
    const TArray<FString> Roots = GetContentRoots();
    FString Scope = Folder.TrimStartAndEnd();
    Scope.RemoveFromEnd(TEXT("/"));
    if (!Scope.IsEmpty() && !IsInFolders(Scope, Roots))
    {
        OutError = FText::FromString(TEXT("Choose /Game, a subfolder, or a project plugin content folder. Leave blank for all project content."));
        return false;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    if (Registry.IsLoadingAssets())
    {
        OutError = FText::FromString(TEXT("The Asset Registry is still discovering content. Click Scan project again after discovery finishes."));
        return false;
    }
    FARFilter Filter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
    Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
#else
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
#endif
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    if (!Scope.IsEmpty()) { Filter.PackagePaths.Add(FName(*Scope)); }
    else { for (const FString& Root : Roots) { Filter.PackagePaths.Add(FName(*Root)); } }
    if (!Registry.GetAssets(Filter, OutAssets))
    {
        OutError = FText::FromString(TEXT("Could not query Blueprint assets from the Asset Registry."));
        return false;
    }
    OutAssets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
    return true;
}
