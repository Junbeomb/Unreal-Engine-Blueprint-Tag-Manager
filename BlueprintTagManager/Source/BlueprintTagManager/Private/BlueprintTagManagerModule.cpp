#include "Modules/ModuleManager.h"

#include "Framework/Docking/TabManager.h"
#include "SBlueprintTagManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "BlueprintTagManager"

class FBlueprintTagManagerModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
            FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
            {
                return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ SNew(SBlueprintTagManager) ];
            }))
            .SetDisplayName(LOCTEXT("Title", "Blueprint Tag Manager"))
            .SetTooltipText(LOCTEXT("Tip", "Manage tags across all project Actor Blueprints."))
            .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Tag"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintTagManagerModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        if (const auto Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(TabId)) { Tab->RequestCloseTab(); }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
    }
    virtual bool SupportsDynamicReloading() override { return false; }

private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        const FUIAction Open(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(TabId); }));
        for (const FName MenuName : { FName("LevelEditor.MainMenu.Tools"), FName("LevelEditor.MainMenu.Window") })
        {
            UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName);
            Menu->FindOrAddSection("BlueprintTagManager").AddMenuEntry("OpenBlueprintTagManager",
                LOCTEXT("Menu", "Blueprint Tag Manager"), LOCTEXT("MenuTip", "Scan project Blueprints and bulk edit actor and component tags."),
                FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Tag"), Open);
        }
        UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User")->FindOrAddSection("BlueprintTagManager")
            .AddEntry(FToolMenuEntry::InitToolBarButton("OpenBlueprintTagManager", Open,
                LOCTEXT("Toolbar", "Blueprint Tags"), LOCTEXT("ToolbarTip", "Open the project-wide Blueprint Tag Manager."),
                FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Tag")));
    }
    static const FName TabId;
};

const FName FBlueprintTagManagerModule::TabId(TEXT("BlueprintTagManager.Project"));
IMPLEMENT_MODULE(FBlueprintTagManagerModule, BlueprintTagManager)
#undef LOCTEXT_NAMESPACE
