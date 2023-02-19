// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

#include "LiveBlueprintDebugger.h"
#include "LiveBlueprintDebuggerDetailCustomization.h"

#include "ActorDetailsDelegates.h"
#include "LevelEditor.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FLiveBlueprintDebuggerModule"

DEFINE_LOG_CATEGORY(LogLiveBlueprintDebugger);

void FLiveBlueprintDebuggerModule::StartupModule()
{
	// Register extended actor details provider.
	UE_LOG(LogLiveBlueprintDebugger, Verbose, TEXT("Initializing LiveBlueprintDebugger module."));
	DetailCustomizationDelegateHandle = OnExtendActorDetails.AddLambda(
		[this](class IDetailLayoutBuilder& DetailBuilder, const FGetSelectedActors& /*GetSelectedActorsDelegate*/)
		{
			CurrentDetailCustomization.Reset();
			CurrentDetailCustomization = FLiveBlueprintDebuggerDetailCustomization::CreateForLayoutBuilder(DetailBuilder);
		});
}

void FLiveBlueprintDebuggerModule::ShutdownModule()
{
	OnExtendActorDetails.Remove(DetailCustomizationDelegateHandle);
	DetailCustomizationDelegateHandle.Reset();
	CurrentDetailCustomization.Reset();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveBlueprintDebuggerModule, LiveBlueprintDebugger)