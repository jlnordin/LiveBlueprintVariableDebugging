// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveBlueprintVariables.h"
#include "LiveBlueprintVariablesDetailCustomization.h"

#include "ActorDetailsDelegates.h"
#include "LevelEditor.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FLiveBlueprintVariablesModule"

DEFINE_LOG_CATEGORY(LogLiveBlueprintVariables);

void FLiveBlueprintVariablesModule::StartupModule()
{
	// Register extended actor details provider.
	UE_LOG(LogLiveBlueprintVariables, Verbose, TEXT("Initializing LiveBlueprintVariables module."));
	DetailCustomizationDelegateHandle = OnExtendActorDetails.AddLambda(
		[this](class IDetailLayoutBuilder& DetailBuilder, const FGetSelectedActors& /*GetSelectedActorsDelegate*/)
		{
			CurrentDetailCustomization.Reset();
			CurrentDetailCustomization = FLiveBlueprintVariablesDetailCustomization::CreateForLayoutBuilder(DetailBuilder);
		});
}

void FLiveBlueprintVariablesModule::ShutdownModule()
{
	OnExtendActorDetails.Remove(DetailCustomizationDelegateHandle);
	DetailCustomizationDelegateHandle.Reset();
	CurrentDetailCustomization.Reset();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveBlueprintVariablesModule, LiveBlueprintVariables)