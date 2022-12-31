#include "LiveBlueprintVariablesDetailCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Brushes/SlateColorBrush.h"
#include "LiveBlueprintVariables.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FLiveBlueprintVariablesModule"

static const FString c_PrivateCategoryName = "Private Implementation Variables";
static const float c_PropertyRefreshPeriodInSeconds = 0.1f;
static const FSlateColorBrush c_HighlightedBackgroundBrush = FSlateColorBrush(FLinearColor(0.0f, 1.0f, 0.0f, 0.6f));

TUniquePtr<FLiveBlueprintVariablesDetailCustomization> FLiveBlueprintVariablesDetailCustomization::CreateForLayoutBuilder(
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

	if (Actor->GetWorld()->WorldType != EWorldType::PIE)
	{
		UE_LOG(
			LogLiveBlueprintVariables,
			Verbose,
			TEXT("Live Blueprint Variables only supported when playing or simulating in the editor."));

		return nullptr;
	}

	return TUniquePtr<FLiveBlueprintVariablesDetailCustomization>(
		new FLiveBlueprintVariablesDetailCustomization{Actor, LayoutBuilder});
}

TWeakObjectPtr<AActor> FLiveBlueprintVariablesDetailCustomization::GetActorToCustomize(IDetailLayoutBuilder& LayoutBuilder)
{
    TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
    LayoutBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

    if (ObjectsBeingCustomized.IsEmpty())
    {
        UE_LOG(
            LogLiveBlueprintVariables,
            Display,
            TEXT("No objects selected."));

        return nullptr;
    }

    if (ObjectsBeingCustomized.Num() > 1)
    {
        UE_LOG(
            LogLiveBlueprintVariables,
            Display,
            TEXT("Blueprint details only support one object, but multiple objects are selected. Showing data from the first selected object."));

        return nullptr;
    }

    auto Object = ObjectsBeingCustomized[0];

    if (!Object.IsValid())
    {
        UE_LOG(
            LogLiveBlueprintVariables,
            Display,
            TEXT("Selected object is invalid."));

        return nullptr;
    }
	
    return TWeakObjectPtr<AActor>(CastChecked<AActor>(Object));
}

bool FLiveBlueprintVariablesDetailCustomization::IsAnyAncestorABlueprintClass(UClass* Class)
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

FLiveBlueprintVariablesDetailCustomization::FLiveBlueprintVariablesDetailCustomization(
	TWeakObjectPtr<AActor> ActorToCustomize,
	IDetailLayoutBuilder& LayoutBuilder) :
		Actor(ActorToCustomize)
{
	UE_LOG(
		LogLiveBlueprintVariables,
		Verbose,
		TEXT("Customizing Actor '%s'..."),
		*Actor->GetName());

	// Categorize and sort the properties associated with this Blueprint class.
	TMap<FString, TArray<FProperty*>> PropertiesByCategory;

	for (UClass* Class = Actor->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
	{
		if (Class->ClassGeneratedBy == nullptr)
		{
			// Only add details for Blueprint-generated classes.
			continue;
		}

		UE_LOG(
			LogLiveBlueprintVariables,
			Verbose,
			TEXT("  Class: '%s'"),
			*Class->GetName());

		FProperty* LastClassProperty = (Class->GetSuperClass() != nullptr) ? Class->GetSuperClass()->PropertyLink : nullptr;
		for (FProperty* Property = Class->PropertyLink; Property != LastClassProperty; Property = Property->PropertyLinkNext)
		{
			if (EnumHasAnyFlags(Property->PropertyFlags, CPF_Transient | CPF_OutParm))
			{
				continue;
			}

			PropertiesByCategory.FindOrAdd(GetPropertyCategoryString(Property)).Add(Property);
		}
	}

	// Add the Blueprint details section.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<FPropertySection> BlueprintSection = PropertyModule.FindOrCreateSection("Actor", "Blueprint", LOCTEXT("BlueprintSection", "Blueprint"));

	// Add widgets for all of the categories and properties.
	for (auto& [CategoryString, PropertyArray] : PropertiesByCategory)
	{
		FName CategoryName = *FString::Printf(TEXT("Blueprint Properties - %s"), *CategoryString);

		if (!BlueprintSection->HasAddedCategory(CategoryName))
		{
			BlueprintSection->AddCategory(CategoryName);
		}

		IDetailCategoryBuilder& BlueprintCategory = LayoutBuilder.EditCategory(CategoryName);

		for (auto& Property : PropertyArray)
		{
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property); StructProperty != nullptr)
			{
				IDetailGroup& StructGroup = BlueprintCategory.AddGroup(
					StructProperty->GetFName(),
					FText::FromString(StructProperty->GetName()),
					false,
					true);

				ExpandStructProperty(StructProperty, &StructGroup, Actor.Get());
			}
			else
			{
				const bool bAdvancedDisplay =
					EnumHasAnyFlags(Property->PropertyFlags, CPF_AdvancedDisplay) ||
					(CategoryString == c_PrivateCategoryName);

				FLiveBlueprintWidgetRow NewRow = {
					&(BlueprintCategory.AddCustomRow(Property->GetDisplayNameText(), bAdvancedDisplay)),
					Actor.Get(),
					Property,
					0,
					0
				};

				WidgetRows.Add(NewRow);
				FillInWidgetRow(NewRow);
			}
		}
	}

	// Register a timer to keep our values up-to-date.
	Actor->GetWorldTimerManager().SetTimer(
		UpdateTimerHandle,
		[this]()
		{
			UpdateBlueprintDetails();
		},
		c_PropertyRefreshPeriodInSeconds,
		true);
}

FLiveBlueprintVariablesDetailCustomization::~FLiveBlueprintVariablesDetailCustomization()
{
	if (Actor.IsValid())
	{
		Actor->GetWorldTimerManager().ClearTimer(UpdateTimerHandle);
	}
}

void FLiveBlueprintVariablesDetailCustomization::UpdateBlueprintDetails()
{
	if (!Actor.IsValid())
	{
		return;
	}

	double RealTimeInSeconds = Actor->GetWorld()->GetRealTimeSeconds();

	for (auto& Row : WidgetRows)
	{
		UpdateWidgetRowValue(Row, RealTimeInSeconds);
	}
}

void FLiveBlueprintVariablesDetailCustomization::ExpandStructProperty(
    FStructProperty* StructProperty, 
    IDetailGroup* StructGroup, 
    void* Container)
{
    for (FProperty* InnerProperty = StructProperty->Struct->PropertyLink; InnerProperty != nullptr; InnerProperty = InnerProperty->PropertyLinkNext)
    {
        if (FStructProperty* StructSubProperty = CastField<FStructProperty>(InnerProperty); StructSubProperty != nullptr)
        {
            IDetailGroup& StructSubGroup = StructGroup->AddGroup(
                StructSubProperty->GetFName(),
                FText::FromString(StructSubProperty->GetName()),
                true);

            ExpandStructProperty(StructSubProperty, &StructSubGroup, StructProperty->ContainerPtrToValuePtr<void>(Container));
        }
        else
        {
			FLiveBlueprintWidgetRow NewRow = {
				&(StructGroup->AddWidgetRow()),
				StructProperty->ContainerPtrToValuePtr<void>(Container),
				InnerProperty,
				0,
				0
			};

			WidgetRows.Add(NewRow);
			FillInWidgetRow(NewRow);
        }
    }
}

void FLiveBlueprintVariablesDetailCustomization::FillInWidgetRow(
	FLiveBlueprintWidgetRow& LiveBlueprintWidgetRow)
{
	LiveBlueprintWidgetRow.ValueHash = GetPropertyValueHash(LiveBlueprintWidgetRow.Container,
		LiveBlueprintWidgetRow.Property);

    FString ValueString = GetPropertyValueString(
		LiveBlueprintWidgetRow.Container, 
		LiveBlueprintWidgetRow.Property);

    UE_LOG(
        LogLiveBlueprintVariables,
		Verbose,
        TEXT("  Property: '%s' [%s] \tFlags: 0x%08X\tValue: %s"),
        *LiveBlueprintWidgetRow.Property->GetName(),
        *LiveBlueprintWidgetRow.Property->GetClass()->GetName(),
		LiveBlueprintWidgetRow.Property->GetPropertyFlags(),
        *ValueString);

	LiveBlueprintWidgetRow.WidgetRow
        ->NameContent()
        [
            SNew(STextBlock)
            .Text(LiveBlueprintWidgetRow.Property->GetDisplayNameText())
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
        .ValueContent()
        [
			SNew(SBorder)
			.HAlign(EHorizontalAlignment::HAlign_Fill)
			.VAlign(EVerticalAlignment::VAlign_Fill)
			.BorderBackgroundColor(FColor::Transparent)
			.BorderImage(&c_HighlightedBackgroundBrush)
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(ValueString))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
        ];
}

void FLiveBlueprintVariablesDetailCustomization::UpdateWidgetRowValue(
	FLiveBlueprintWidgetRow& LiveBlueprintWidgetRow,
	double RealTimeInSeconds)
{
	SBorder& Border = static_cast<SBorder&>(LiveBlueprintWidgetRow.WidgetRow->ValueWidget.Widget.Get());
	STextBlock& TextBlock = static_cast<STextBlock&>(Border.GetContent().Get());

	uint32 NewValueHash = GetPropertyValueHash(
		LiveBlueprintWidgetRow.Container,
		LiveBlueprintWidgetRow.Property);

	if (NewValueHash != LiveBlueprintWidgetRow.ValueHash)
	{
		FString ValueString = GetPropertyValueString(
			LiveBlueprintWidgetRow.Container,
			LiveBlueprintWidgetRow.Property);
		
		TextBlock.SetText(FText::FromString(ValueString));

		LiveBlueprintWidgetRow.ValueHash = NewValueHash;
		LiveBlueprintWidgetRow.LastUpdateTimeInSeconds = RealTimeInSeconds;
	}

	double TimeSincePropertyChanged = (RealTimeInSeconds - LiveBlueprintWidgetRow.LastUpdateTimeInSeconds);
	if (TimeSincePropertyChanged <= 2.0)
	{
		FLinearColor BackgroundColor = FLinearColor::LerpUsingHSV(
			FLinearColor::Green,
			FLinearColor::Transparent,
			static_cast<float>(std::clamp(TimeSincePropertyChanged, 0.0, 1.0)));

		Border.SetBorderBackgroundColor(BackgroundColor);
	}
}

FString FLiveBlueprintVariablesDetailCustomization::GetPropertyCategoryString(FProperty* Property)
{
    FString Category = c_PrivateCategoryName;
    
    auto MetaDataMap = Property->GetMetaDataMap();
    
    if (MetaDataMap != nullptr &&
        MetaDataMap->Contains(FName("Category")))
    {
        Category = *(MetaDataMap->Find(FName("Category")));
    }

    return Category;
}

FString FLiveBlueprintVariablesDetailCustomization::GetPropertyValueString(void* Container, FProperty* Property)
{
    FString ValueString;

    if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property); FloatProperty != nullptr)
    {
        ValueString = FString::Printf(
            TEXT("%f"),
            FloatProperty->GetPropertyValue_InContainer(Container)); 
    }
    else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property); DoubleProperty != nullptr)
    {
        ValueString = FString::Printf(
            TEXT("%lf"),
            DoubleProperty->GetPropertyValue_InContainer(Container));
    }
    else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty != nullptr)
    {
        ValueString = FString::Printf(
            TEXT("%i"),
            ByteProperty->GetPropertyValue_InContainer(Container));
    }
    else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property); ObjectProperty != nullptr)
    {
        TObjectPtr<UObject> ObjectPropertyValue = ObjectProperty->GetPropertyValue_InContainer(Container);

        ValueString = FString::Printf(
            TEXT("%s"),
            ObjectPropertyValue != nullptr ? *(ObjectPropertyValue->GetName()) : TEXT("[nullptr]"));
    }
    else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property); BoolProperty != nullptr)
    {
        ValueString = FString::Printf(
            TEXT("%s"),
            BoolProperty->GetPropertyValue_InContainer(Container) ? TEXT("true") : TEXT("false"));
    }
    else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property); IntProperty != nullptr)
    {
        ValueString = FString::Printf(
            TEXT("%i"),
            IntProperty->GetPropertyValue_InContainer(Container));
    }
    else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property); NameProperty != nullptr)
    {
        ValueString = NameProperty->GetPropertyValue_InContainer(Container).ToString();
    }
    else if (FStrProperty* StrProperty = CastField<FStrProperty>(Property); StrProperty != nullptr)
    {
        ValueString = StrProperty->GetPropertyValue_InContainer(Container);
    }
    else
    {
        ValueString = FString::Printf(
            TEXT("[%s]"),
            *Property->GetClass()->GetName());
    }

    return ValueString;
}

uint32 FLiveBlueprintVariablesDetailCustomization::GetPropertyValueHash(void* Container, FProperty* Property)
{
	uint32 ValueHash = 0;

	if (Property->PropertyFlags & CPF_HasGetValueTypeHash)
	{
		ValueHash = Property->GetValueTypeHash(Property->ContainerPtrToValuePtr<void>(Container));
	}

	return ValueHash;
}
