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

	PIEStartingDelegateHandle = FEditorDelegates::PreBeginPIE.AddLambda(
		[this](bool)
		{
			if (CurrentDetailCustomization != nullptr)
			{
				CurrentDetailCustomization->SaveSelectedActor();
			}
		});

	PIEStartedDelegateHandle = FEditorDelegates::PostPIEStarted.AddLambda(
		[this](bool) 
		{
			if (CurrentDetailCustomization != nullptr)
			{
				CurrentDetailCustomization->ReselectActor();
			}
		});

	OnPreSwitchBeginPIEAndSIEDelegateHandle = FEditorDelegates::OnPreSwitchBeginPIEAndSIE.AddLambda(
		[this](bool)
		{
			if (CurrentDetailCustomization != nullptr)
			{
				CurrentDetailCustomization->SaveSelectedActor();
			}
		});

	OnSwitchBeginPIEAndSIEDelegateHandle = FEditorDelegates::OnSwitchBeginPIEAndSIE.AddLambda(
		[this](bool)
		{
			if (CurrentDetailCustomization != nullptr)
			{
				CurrentDetailCustomization->ReselectActor();
			}
		});
}

void FLiveBlueprintDebuggerModule::ShutdownModule()
{
	FEditorDelegates::OnSwitchBeginPIEAndSIE.Remove(OnSwitchBeginPIEAndSIEDelegateHandle);
	FEditorDelegates::OnPreSwitchBeginPIEAndSIE.Remove(OnPreSwitchBeginPIEAndSIEDelegateHandle);
	FEditorDelegates::PostPIEStarted.Remove(PIEStartedDelegateHandle);
	FEditorDelegates::PreBeginPIE.Remove(PIEStartingDelegateHandle);
	OnExtendActorDetails.Remove(DetailCustomizationDelegateHandle);
	DetailCustomizationDelegateHandle.Reset();
	CurrentDetailCustomization.Reset();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveBlueprintDebuggerModule, LiveBlueprintDebugger)