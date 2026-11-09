#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

/** Asset discovery does not require an asset editor to be opened. */
class FBlueprintTagProject
{
public:
    static TArray<FString> GetContentRoots();
    static bool Discover(const FString& Folder, TArray<FAssetData>& OutAssets, FText& OutError);
    static bool IsInFolders(const FString& PackageName, const TArray<FString>& Folders);
};
