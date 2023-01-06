#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

struct FLiveBlueprintWidgetRow
{
	FDetailWidgetRow* WidgetRow = nullptr;
	void* Container = nullptr;
	FProperty* Property = nullptr;
	double LastUpdateTimeInSeconds = 0.0;
	uint32 ValueHash = 0;
};

/**
 * Adds additional detail customizations for any Actor class that also has Blueprint variables.
 */
class FLiveBlueprintVariablesDetailCustomization
{
public:
	static TUniquePtr<FLiveBlueprintVariablesDetailCustomization> CreateForLayoutBuilder(
		IDetailLayoutBuilder& LayoutBuilder);

private:
	static TWeakObjectPtr<AActor> GetActorToCustomize(IDetailLayoutBuilder& LayoutBuilder);
	static bool IsAnyAncestorABlueprintClass(UClass* Class);

private:
	FLiveBlueprintVariablesDetailCustomization(TWeakObjectPtr<AActor> ActorToCustomize, IDetailLayoutBuilder& LayoutBuilder);
public:
	~FLiveBlueprintVariablesDetailCustomization();
	
private:
	void UpdateBlueprintDetails();

	void ExpandStructProperty(FStructProperty* StructProperty, class IDetailGroup* StructGroup, void* Container);
	
	static void FillInWidgetRow(FLiveBlueprintWidgetRow& LiveBlueprintWidgetRow);
	static void UpdateWidgetRowValue(FLiveBlueprintWidgetRow& LiveBlueprintWidgetRow, double RealTimeInSeconds);
	static FString GetPropertyCategoryString(FProperty* Property);
	static FString GetPropertyValueString(void* Container, FProperty* Property);
	static uint32 GetPropertyValueHash(void* Container, FProperty* Property);

	TWeakObjectPtr<AActor> Actor;
	TArray<FLiveBlueprintWidgetRow> WidgetRows;
	FTimerHandle UpdateTimerHandle;
	TSharedPtr<class SKismetDebugTreeView> DebugTreeWidget;
	TSharedPtr<class FDebugLineItem> RootDebugTreeItem;
};