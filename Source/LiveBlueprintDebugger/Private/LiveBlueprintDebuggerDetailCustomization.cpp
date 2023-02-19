// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

#include "LiveBlueprintDebuggerDetailCustomization.h"

#include <chrono>
#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Brushes/SlateColorBrush.h"
#include "Debugging/SKismetDebugTreeView.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "LiveBlueprintDebugger.h"
#include "LiveBlueprintDebuggerSettings.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FLiveBlueprintDebuggerModule"

static const FString c_PrivateCategoryName = "Private Implementation Variables";
static const FSlateColorBrush c_HighlightedBackgroundBrush = FSlateColorBrush(FLinearColor::White);


TUniquePtr<FLiveBlueprintDebuggerDetailCustomization> FLiveBlueprintDebuggerDetailCustomization::CreateForLayoutBuilder(
	IDetailLayoutBuilder& LayoutBuilder)
{
	TWeakObjectPtr<AActor> Actor = GetActorToCustomize(LayoutBuilder);

    if (Actor == nullptr)
    {
        return nullptr;
    }

    if (!IsAnyAncestorABlueprintClass(Actor->GetClass()))
    {
        return nullptr;
    }

	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	if ((Settings->WhenToShowVariables == EShowBlueprintVariables::OnlyWhenPlayingOrSimulating) &&
		(Actor->GetWorld()->WorldType != EWorldType::PIE))
	{
		UE_LOG(
			LogLiveBlueprintDebugger,
			Verbose,
			TEXT("Live Blueprint Variables configured to only show when playing or simulating in the editor."));

		return nullptr;
	}

	return TUniquePtr<FLiveBlueprintDebuggerDetailCustomization>(
		new FLiveBlueprintDebuggerDetailCustomization{Actor, LayoutBuilder});
}

TWeakObjectPtr<AActor> FLiveBlueprintDebuggerDetailCustomization::GetActorToCustomize(IDetailLayoutBuilder& LayoutBuilder)
{
    TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
    LayoutBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

    if (ObjectsBeingCustomized.IsEmpty())
    {
        UE_LOG(
            LogLiveBlueprintDebugger,
            Display,
            TEXT("No objects selected."));

        return nullptr;
    }

    if (ObjectsBeingCustomized.Num() > 1)
    {
        UE_LOG(
            LogLiveBlueprintDebugger,
            Display,
            TEXT("Blueprint details only support one object."));

        return nullptr;
    }

    auto Object = ObjectsBeingCustomized[0];

    if (!Object.IsValid())
    {
        UE_LOG(
            LogLiveBlueprintDebugger,
            Display,
            TEXT("Selected object is invalid."));

        return nullptr;
    }
	
    return TWeakObjectPtr<AActor>(CastChecked<AActor>(Object));
}

bool FLiveBlueprintDebuggerDetailCustomization::IsAnyAncestorABlueprintClass(UClass* Class)
{
	while (Class != nullptr)
	{
		if (Class->ClassGeneratedBy != nullptr)
		{
			return true;
		}

		Class = Class->GetSuperClass();
	}

	return false;
}

FLiveBlueprintDebuggerDetailCustomization::FLiveBlueprintDebuggerDetailCustomization(
	TWeakObjectPtr<AActor> ActorToCustomize,
	IDetailLayoutBuilder& LayoutBuilder) :
		Actor(ActorToCustomize)
{
	UE_LOG(
		LogLiveBlueprintDebugger,
		Verbose,
		TEXT("Customizing Actor '%s'..."),
		*Actor->GetName());

	// Categorize and sort the properties associated with this Blueprint class.
	TMap<FString, TArray<FFastPropertyInstanceInfo>> PropertiesByCategory;
	for (auto Iterator = TFieldIterator<FProperty>(Actor->GetClass()); Iterator != nullptr; ++Iterator)
	{
		FProperty* Property = *Iterator;

		if (Property->HasAllPropertyFlags(CPF_BlueprintVisible))
		{
			PropertiesByCategory.FindOrAdd(GetPropertyCategoryString(Property)).Add(
				{ 
					FFastPropertyInstanceInfo(Actor.Get(), Property)
				});
		}
	}

	// Add the Blueprint details section.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<FPropertySection> BlueprintSection = PropertyModule.FindOrCreateSection("Actor", "Blueprint", LOCTEXT("BlueprintSection", "Blueprint"));

	// Add widgets for all of the categories and properties.
	for (auto& [CategoryString, PropertyInfoArray] : PropertiesByCategory)
	{
		FName CategoryName = *FString::Printf(TEXT("Blueprint Properties - %s"), *CategoryString);

		if (!BlueprintSection->HasAddedCategory(CategoryName))
		{
			BlueprintSection->AddCategory(CategoryName);
		}

		IDetailCategoryBuilder& BlueprintCategory = LayoutBuilder.EditCategory(CategoryName);
		
		for (auto& PropertyInstanceInfo : PropertyInfoArray)
		{
			if (FFastPropertyInstanceInfo::ShouldExpandProperty(PropertyInstanceInfo))
			{
				ExpandPropertyChildren(
					BlueprintCategory.AddGroup(
						PropertyInstanceInfo.GetProperty()->GetFName(),
						PropertyInstanceInfo.GetDisplayName()),
					PropertyInstanceInfo);
			}
			else
			{
				const bool bAdvancedDisplay =
					PropertyInstanceInfo.GetProperty()->HasAnyPropertyFlags(CPF_AdvancedDisplay) ||
					(CategoryString == c_PrivateCategoryName);

				FLiveBlueprintWidgetRowData NewRowData { PropertyInstanceInfo };

				FillInWidgetRow(
					BlueprintCategory.AddCustomRow(PropertyInstanceInfo.GetProperty()->GetDisplayNameText(), bAdvancedDisplay),
					NewRowData,
					0);

				WidgetRows.Add(NewRowData);
			}
		}
	}

	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	// Register a timer to keep our values up-to-date.
	if (Actor->GetWorld()->WorldType == EWorldType::PIE &&
		Settings->PropertyRefreshRate != EPropertyRefreshRate::NoLiveUpdates)
	{
		float RefreshPeriod = 1.0f;

		switch (Settings->PropertyRefreshRate)
		{
			default:
			case EPropertyRefreshRate::One:
			{
				RefreshPeriod = 1.0f;
				break;
			}

			case EPropertyRefreshRate::Ten:
			{
				RefreshPeriod = 0.1f;
				break;
			}

			case EPropertyRefreshRate::Thirty:
			{
				RefreshPeriod = 0.0334f;
				break;
			}
		}

		Actor->GetWorldTimerManager().SetTimer(
			UpdateTimerHandle,
			[this]()
			{
				UpdateBlueprintDetails();
			},
			RefreshPeriod,
			true);
	}
}

FLiveBlueprintDebuggerDetailCustomization::~FLiveBlueprintDebuggerDetailCustomization()
{
	if (Actor.IsValid() && UpdateTimerHandle.IsValid())
	{
		Actor->GetWorldTimerManager().ClearTimer(UpdateTimerHandle);
	}
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateBlueprintDetails()
{
	if (!Actor.IsValid())
	{
		return;
	}

	double RealTimeInSeconds = Actor->GetWorld()->GetRealTimeSeconds();

	for (auto& Row : WidgetRows)
	{
		UpdateWidgetRow(Row, RealTimeInSeconds);
	}
}

void FLiveBlueprintDebuggerDetailCustomization::ExpandPropertyChildren(
	IDetailGroup& Group,
	FFastPropertyInstanceInfo& PropertyInstanceInfo,
	int LevelsOfRecursion)
{
	// Fill in the group's header row.
	FLiveBlueprintWidgetRowData HeaderRowData {PropertyInstanceInfo};
	FillInWidgetRow(Group.HeaderRow(), HeaderRowData, LevelsOfRecursion * 2 + 2);
	WidgetRows.Add(HeaderRowData);

	for (auto& ChildPropertyInfo : PropertyInstanceInfo.GetChildren())
	{
        if (FFastPropertyInstanceInfo::ShouldExpandProperty(ChildPropertyInfo) && LevelsOfRecursion < 5)
        {
            IDetailGroup& SubGroup = Group.AddGroup(
				ChildPropertyInfo.GetProperty()->GetFName(),
				ChildPropertyInfo.GetDisplayName(),
                false);

			ExpandPropertyChildren(
				SubGroup,
				ChildPropertyInfo,
				LevelsOfRecursion + 1);
        }
        else
        {
			FLiveBlueprintWidgetRowData NewRowData { ChildPropertyInfo };
			FillInWidgetRow(Group.AddWidgetRow(), NewRowData, LevelsOfRecursion * 2 + 2);
			WidgetRows.Add(NewRowData);
        }
    }
}

void FLiveBlueprintDebuggerDetailCustomization::FillInWidgetRow(
	FDetailWidgetRow& WidgetRow,
	FLiveBlueprintWidgetRowData& WidgetRowData,
	int LogIndentation)
{
	WidgetRowData.ValueHash = WidgetRowData.PropertyInstanceInfo.GetValueHash();

	FString Indentation = std::wstring(LogIndentation, L' ').c_str();

    UE_LOG(
        LogLiveBlueprintDebugger,
		Verbose,
        TEXT("%sProperty: '%s' [%s] \tFlags: 0x%08X\tHash: %i"),
		*Indentation,
        *WidgetRowData.PropertyInstanceInfo.GetProperty()->GetName(),
        *WidgetRowData.PropertyInstanceInfo.GetProperty()->GetClass()->GetName(),
		WidgetRowData.PropertyInstanceInfo.GetProperty()->GetPropertyFlags(),
		WidgetRowData.ValueHash);

	WidgetRow
		.NameContent()
		[
			SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				GenerateNameIcon(WidgetRowData.PropertyInstanceInfo)
			]
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.f, 0.f, 0.f, 0.f)
			[
				GenerateNameWidget(WidgetRowData.PropertyInstanceInfo)
			]
		]
		.ValueContent()
		[
			SAssignNew(WidgetRowData.ValueBorderWidget, SBorder)
			.HAlign(EHorizontalAlignment::HAlign_Fill)
			.VAlign(EVerticalAlignment::VAlign_Fill)
			.BorderBackgroundColor(FColor::Transparent)
			.BorderImage(&c_HighlightedBackgroundBrush)
			.Content()
			[
				SAssignNew(WidgetRowData.ValueWidgetContainer, SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.Padding(.5f, 1.f)
			]
		];

	UpdateWidgetRowValue(WidgetRowData);
}

TSharedRef<SWidget> FLiveBlueprintDebuggerDetailCustomization::GenerateNameIcon(
	const FFastPropertyInstanceInfo& PropertyInstanceInfo)
{
	// Unreal 5.1 has a bug where calling FDebugLineItem::GetNameIcon will crash if the given 
	// property falls back to the default icon. So instead of relying on this, we create our 
	// own icon in a similar way to the Kismet / Blueprint Debugger here.
	
	FSlateColor BaseColor;
	FSlateColor UnusedColor;
	FSlateBrush const* UnusedIcon = nullptr;
	const FSlateBrush* IconBrush = FBlueprintEditor::GetVarIconAndColorFromProperty(
		PropertyInstanceInfo.GetProperty().Get(),
		BaseColor,
		UnusedIcon,
		UnusedColor
	);

	return SNew(SImage)
		.Image(IconBrush)
		.ColorAndOpacity(BaseColor)
		.ToolTipText(PropertyInstanceInfo.GetType());
}

TSharedRef<SWidget> FLiveBlueprintDebuggerDetailCustomization::GenerateNameWidget(
	const FFastPropertyInstanceInfo& PropertyInstanceInfo)
{
	return SNew(STextBlock)
		.Text(PropertyInstanceInfo.GetDisplayName())
		.ToolTipText(PropertyInstanceInfo.GetDisplayName());
}

TSharedRef<SWidget> FLiveBlueprintDebuggerDetailCustomization::GenerateValueWidget(
	const FFastPropertyInstanceInfo& PropertyInstanceInfo)
{
	FText ValueText;

	if (PropertyInstanceInfo.GetProperty()->IsA<FObjectProperty>() || 
		PropertyInstanceInfo.GetProperty()->IsA<FInterfaceProperty>())
	{
		FString ValueAsString;
		if (PropertyInstanceInfo.IsValid() && (PropertyInstanceInfo.GetObject().Get() != nullptr))
		{
			ValueAsString = FString::Printf(
				TEXT("%s (Class: %s)"),
				*PropertyInstanceInfo.GetObject()->GetName(),
				*PropertyInstanceInfo.GetObject()->GetClass()->GetName());
		}
		else
		{
			ValueAsString = "None";
		}

		ValueText = FText::FromString(ValueAsString);
	}
	else
	{
		const FString ValueAsString = PropertyInstanceInfo.GetValue().ToString();
		ValueText = FText::FromString(ValueAsString.Replace(TEXT("\n"), TEXT(" ")));
	}

	return SNew(STextBlock)
		.Text(ValueText)
		.ToolTipText(ValueText);
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateWidgetRowValue(FLiveBlueprintWidgetRowData& WidgetRowData)
{
	// We have special handling for set, array, and map properties such that their immediate children 
	// are also included in the ValueWidgetContainer. This allows the number of elements to change 
	// dynamically without needing to add a new row to the Blueprint details category, which is not 
	// feasible after it has been constructed.

	if (WidgetRowData.PropertyInstanceInfo.GetProperty()->IsA<FSetProperty>() ||
		WidgetRowData.PropertyInstanceInfo.GetProperty()->IsA<FArrayProperty>() ||
		WidgetRowData.PropertyInstanceInfo.GetProperty()->IsA<FMapProperty>())
	{
		auto VerticalBox = SNew(SVerticalBox);
		
		for (auto& ChildPropertyInfo : WidgetRowData.PropertyInstanceInfo.GetChildren())
		{
			if (WidgetRowData.PropertyInstanceInfo.GetProperty()->IsA<FMapProperty>())
			{
				VerticalBox->AddSlot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							GenerateNameWidget(ChildPropertyInfo)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						.Padding(.5f, 1.f)
						[
							GenerateValueWidget(ChildPropertyInfo)
						]
					];
			}
			else
			{
				VerticalBox->AddSlot()
					.AutoHeight()
					[
						GenerateValueWidget(ChildPropertyInfo)
					];
			}
		}

		if (WidgetRowData.PropertyInstanceInfo.GetChildren().IsEmpty())
		{
			VerticalBox->AddSlot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString("[empty]"))
				];
		}

		WidgetRowData.ValueWidgetContainer->GetSlot(0)
			[
				VerticalBox
			];
	}
	else
	{
		WidgetRowData.ValueWidgetContainer->GetSlot(0)
			[
				GenerateValueWidget(WidgetRowData.PropertyInstanceInfo)
			];
	}
}

void FLiveBlueprintDebuggerDetailCustomization::UpdateWidgetRow(
	FLiveBlueprintWidgetRowData& WidgetRowData,
	double RealTimeInSeconds)
{
	if (*WidgetRowData.PropertyInstanceInfo.GetProperty() == nullptr)
	{
		return;
	}

	SBorder& Border = *WidgetRowData.ValueBorderWidget;

	uint32 NewValueHash = WidgetRowData.PropertyInstanceInfo.GetValueHash();

	if (NewValueHash != WidgetRowData.ValueHash)
	{
		WidgetRowData.PropertyInstanceInfo.Refresh();
		UpdateWidgetRowValue(WidgetRowData);

		WidgetRowData.ValueHash = WidgetRowData.PropertyInstanceInfo.GetValueHash();
		WidgetRowData.LastUpdateTimeInSeconds = RealTimeInSeconds;
	}

	const ULiveBlueprintDebuggerSettings* Settings = GetDefault<ULiveBlueprintDebuggerSettings>();

	if (Settings->bHighlightValuesThatHaveChanged)
	{
		double TimeSincePropertyChanged = (RealTimeInSeconds - WidgetRowData.LastUpdateTimeInSeconds);
		if (TimeSincePropertyChanged <= 2.0)
		{
			FLinearColor BackgroundColor = Settings->PropertyChangedHighlightColor;
			BackgroundColor.A = BackgroundColor.A * (1.0f - static_cast<float>(std::clamp(TimeSincePropertyChanged, 0.0, 1.0)));
			Border.SetBorderBackgroundColor(BackgroundColor);
		}
	}
}

FString FLiveBlueprintDebuggerDetailCustomization::GetPropertyCategoryString(FProperty* Property)
{
    FString Category = c_PrivateCategoryName;
    
    auto MetaDataMap = Property->GetMetaDataMap();
    
    if (MetaDataMap != nullptr &&
        MetaDataMap->Contains(FName("Category")))
    {
        Category = *(MetaDataMap->Find(FName("Category")));
		Category.ReplaceInline(TEXT("|"), TEXT(" "));
    }

    return Category;
}

TArray<FDebugTreeItemPtr> FLiveBlueprintDebuggerDetailCustomization::GetActorBlueprintPropertiesAsDebugTreeItemPtrs(AActor* Actor)
{
	TArray<FDebugTreeItemPtr> PropertyDebugTreeItems;

	FDebugTreeItemPtr RootDebugTreeItem = SKismetDebugTreeView::MakeParentItem(Actor);
	RootDebugTreeItem->GatherChildrenBase(PropertyDebugTreeItems, FString());
	FDebugTreeItemPtr SelfNode = PropertyDebugTreeItems[0];

	PropertyDebugTreeItems.Reset();
	SelfNode->GatherChildrenBase(PropertyDebugTreeItems, FString());

	return PropertyDebugTreeItems;
}
