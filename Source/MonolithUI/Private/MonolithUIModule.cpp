#include "MonolithUIModule.h"
#include "MonolithUIActions.h"
#include "MonolithUISlotActions.h"
#include "MonolithUITemplateActions.h"
#include "MonolithUIStylingActions.h"
#include "MonolithUIAnimationActions.h"
#include "MonolithUIBindingActions.h"
#include "MonolithUISettingsActions.h"
#include "MonolithUIAccessibilityActions.h"
#include "MonolithSettings.h"
#include "MonolithJsonUtils.h"
#include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "MonolithUI"

void FMonolithUIModule::StartupModule()
{
    if (!GetDefault<UMonolithSettings>()->bEnableUI) return;

    FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();
    FMonolithUIActions::RegisterActions(Registry);
    FMonolithUISlotActions::RegisterActions(Registry);
    FMonolithUITemplateActions::RegisterActions(Registry);
    FMonolithUIStylingActions::RegisterActions(Registry);
    FMonolithUIAnimationActions::RegisterActions(Registry);
    FMonolithUIBindingActions::RegisterActions(Registry);
    FMonolithUISettingsActions::RegisterActions(Registry);
    FMonolithUIAccessibilityActions::RegisterActions(Registry);

    UE_LOG(LogMonolith, Log, TEXT("Monolith — UI module loaded (42 actions)"));
}

void FMonolithUIModule::ShutdownModule()
{
    FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("ui"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithUIModule, MonolithUI)
