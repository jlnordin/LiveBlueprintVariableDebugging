#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

struct FLiveBlueprintWidgetRowData
{
	TSharedPtr<class FDebugLineItem> DebugItem;
	void* Container = nullptr;
	TSharedPtr<struct FPropertyInstanceInfo> PropertyInstanceInfo;
	double LastUpdateTimeInSeconds = 0.0;
	uint32 ValueHash = 0;
	TSharedPtr<class SBorder> BorderWidget;
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

	void ExpandPropertyChildren(
		TSharedPtr<class FDebugLineItem> DebugItem, 
		class IDetailGroup& Group, 
		TSharedPtr<struct FPropertyInstanceInfo> PropertyInstanceInfo,
		void* Container);
	
	static void FillInWidgetRow(FDetailWidgetRow& WidgetRow, FLiveBlueprintWidgetRowData& WidgetRowData);
	static void UpdateWidgetRowValue(FLiveBlueprintWidgetRowData& LiveBlueprintWidgetRow, double RealTimeInSeconds);
	static FString GetPropertyCategoryString(FProperty* Property);
	static FString GetPropertyValueString(void* Container, FProperty* Property);
	static uint32 GetPropertyValueHash(void* Container, const FProperty* Property);
	static TArray<TSharedPtr<class FDebugLineItem>> GetActorBlueprintPropertiesAsDebugTreeItemPtrs(AActor* Actor);
	static TSharedPtr<struct FPropertyInstanceInfo> GetPropertyInstanceInfo(void* Container, const FProperty* Property);
	static bool ShouldExpandProperty(TSharedPtr<struct FPropertyInstanceInfo>& PropertyInstanceInfo);

	TWeakObjectPtr<AActor> Actor;
	TArray<FLiveBlueprintWidgetRowData> WidgetRows;
	FTimerHandle UpdateTimerHandle;
	TSharedPtr<class SKismetDebugTreeView> DebugTreeWidget;
	TSharedPtr<class FDebugLineItem> RootDebugTreeItem;
};