// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

#include "LiveBlueprintDebugger.h"
#include "LiveBlueprintDebuggerDetailCustomization.h"
#include "LiveBlueprintDebuggerSettings.h"

#include "ActorDetailsDelegates.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "PropertyEditorModule.h"
#include "Selection.h"

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

	PreBeginPIEDelegateHandle = FEditorDelegates::PreBeginPIE.AddRaw(
		this, &FLiveBlueprintDebuggerModule::SaveSelectedActor);

	PostPIEStartedDelegateHandle = FEditorDelegates::PostPIEStarted.AddRaw(
		this, &FLiveBlueprintDebuggerModule::ReselectActor);

	OnPreSwitchBeginPIEAndSIEDelegateHandle = FEditorDelegates::OnPreSwitchBeginPIEAndSIE.AddRaw(
		this, &FLiveBlueprintDebuggerModule::SaveSelectedActor);

	OnSwitchBeginPIEAndSIEDelegateHandle = FEditorDelegates::OnSwitchBeginPIEAndSIE.AddRaw(
		this, &FLiveBlueprintDebuggerModule::ReselectActor);
}

void FLiveBlueprintDebuggerModule::ShutdownModule()
{
	FEditorDelegates::OnSwitchBeginPIEAndSIE.Remove(OnSwitchBeginPIEAndSIEDelegateHandle);
	FEditorDelegates::OnPreSwitchBeginPIEAndSIE.Remove(OnPreSwitchBeginPIEAndSIEDelegateHandle);
	FEditorDelegates::PostPIEStarted.Remove(PostPIEStartedDelegateHandle);
	FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEDelegateHandle);
	OnExtendActorDetails.Remove(DetailCustomizationDelegateHandle);
	DetailCustomizationDelegateHandle.Reset();
	CurrentDetailCustomization.Reset();
}

void FLiveBlueprintDebuggerModule::SaveSelectedActor(bool bIsSimulating)
{
	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	if (!Settings->bKeepActorSelected)
	{
		return;
	}
	
	ActorToReselect.Reset();
	
	if (GEditor->GetSelectedActorCount() == 1)
	{
		ActorToReselect = GEditor->GetSelectedActors()->GetTop<AActor>();
	}
}

void FLiveBlueprintDebuggerModule::ReselectActor(bool bIsSimulating)
{
	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	if (!Settings->bKeepActorSelected)
	{
		return;
	}

	AActor* ResolvedActor = ActorToReselect.Get();

	if (ResolvedActor != nullptr)
	{
		GEditor->SelectNone(true, true);
		GEditor->SelectActor(ResolvedActor, true, true);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLiveBlueprintDebuggerModule, LiveBlueprintDebugger)