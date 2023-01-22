#include "FastPropertyInstanceInfo.h"

#include "LiveBlueprintDebugger.h"

#include "Kismet2/KismetDebugUtilities.h"

#define LOCTEXT_NAMESPACE "FLiveBlueprintDebuggerModule"

FFastPropertyInstanceInfo::FFastPropertyInstanceInfo(
	void* Container, 
	const FProperty* Property) :
		ValuePointer(Property->ContainerPtrToValuePtr<void>(Container)),
		Property(Property)
{
	PopulateObject();
	PopulateValueTextAndTypeText();
	PopulateChildren();
}

FFastPropertyInstanceInfo::FFastPropertyInstanceInfo(
	void* ValuePointer,
	TSharedPtr<FPropertyInstanceInfo>& PropertyInstanceInfo) :
		ValuePointer(ValuePointer),
		Property(PropertyInstanceInfo->Property),
		ValueText(PropertyInstanceInfo->Value),
		TypeText(PropertyInstanceInfo->Type),
		Object(PropertyInstanceInfo->Object)
{

}

const TFieldPath<const FProperty>& FFastPropertyInstanceInfo::GetProperty() const
{
	return Property;
}

FText FFastPropertyInstanceInfo::GetDisplayName() const
{
	return Property->GetDisplayNameText();
}

FText FFastPropertyInstanceInfo::GetType() const
{
	return TypeText;
}

FText FFastPropertyInstanceInfo::GetValue() const
{
	return ValueText;
}

void* FFastPropertyInstanceInfo::GetValuePointer() const
{
	return ValuePointer;
}

uint32 FFastPropertyInstanceInfo::GetValueHash() const
{
	uint32 ValueHash = 0;

	if (Property->IsA<FStructProperty>() ||
		Property->IsA<FArrayProperty>() ||
		Property->IsA<FMapProperty>() ||
		Property->IsA<FSetProperty>())
	{
		for (auto& Child : Children)
		{
			ValueHash = HashCombineFast(
				ValueHash,
				Child.GetValueHash());
		}
	}
	else if ((Property->PropertyFlags & CPF_HasGetValueTypeHash))
	{
		ValueHash = Property->GetValueTypeHash(ValuePointer);
	}

	return ValueHash;
}

TArray<FFastPropertyInstanceInfo>& FFastPropertyInstanceInfo::GetChildren()
{
	return Children;
}

bool FFastPropertyInstanceInfo::IsValid() const
{
	return true;
}

const TWeakObjectPtr<UObject>& FFastPropertyInstanceInfo::GetObject() const
{
	return Object;
}

void FFastPropertyInstanceInfo::UpdateValue()
{
	PopulateValueTextAndTypeText();
}

bool FFastPropertyInstanceInfo::ShouldExpandProperty(FFastPropertyInstanceInfo& PropertyInstanceInfo)
{
	return (
		// We only support expanding struct properties in the details panel UI.
		PropertyInstanceInfo.GetProperty()->IsA<FStructProperty>() &&
		PropertyInstanceInfo.GetProperty()->HasAllPropertyFlags(CPF_BlueprintVisible));
}

void FFastPropertyInstanceInfo::PopulateObject()
{
	if (auto ObjectPropertyBase = CastField<FObjectPropertyBase>(*Property); ObjectPropertyBase != nullptr)
	{
		Object = ObjectPropertyBase->GetObjectPropertyValue(ValuePointer);
	}
	else if (auto InterfaceProperty = CastField<FInterfaceProperty>(*Property); InterfaceProperty != nullptr)
	{
		auto InterfaceData = StaticCast<const FScriptInterface*>(ValuePointer);
		Object = InterfaceData->GetObject();
	}
}

void FFastPropertyInstanceInfo::PopulateValueTextAndTypeText()
{
	if (Property->IsA<FObjectPropertyBase>() || 
		Property->IsA<FInterfaceProperty>())
	{
		if (Object.IsValid())
		{
			ValueText = FText::FromString(Object->GetFullName());
			TypeText = Object->GetClass()->GetDisplayNameText();
		}
		else
		{
			ValueText = FText::FromString(L"None");
		}
	}
	else if (auto StructProperty = CastField<FStructProperty>(*Property); StructProperty != nullptr)
	{
		// We explicitly leave the ValueText of a struct empty instead of attempting to summarize all of 
		// its members. We also explicitly avoid calling the FKismetDebugUtilities::GetDebugInfoInternal
		// helper function since it will recursively expand all children of this struct before returning.
		ValueText = {};
		TypeText = StructProperty->GetClass()->GetDisplayNameText();
	}
	else
	{
		// Here we _do_ make use of FKismetDebugUtilities::GetDebugInfoInternal to get the 
		// information we need because we know this is _not_ an unbounded property type that might have
		// hundreds of nested references. That is, this is not an object, interface, or struct.
		
		TSharedPtr<FPropertyInstanceInfo> InstanceInfo;
		FKismetDebugUtilities::GetDebugInfoInternal(
			InstanceInfo,
			*Property,
			ValuePointer);

		ValueText = InstanceInfo->Value;
		TypeText = InstanceInfo->Type;
	}
}

void FFastPropertyInstanceInfo::PopulateChildren()
{
	Children.Empty();

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property.Get()); StructProperty != nullptr)
	{
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It != nullptr; ++It)
		{
			Children.Add(FFastPropertyInstanceInfo{
				ValuePointer,
				*It });
		}
	}
	else if (
		Property->IsA<FSetProperty>() ||
		Property->IsA<FArrayProperty>() ||
		Property->IsA<FMapProperty>())
	{
		TSharedPtr<FPropertyInstanceInfo> InstanceInfo;
		FKismetDebugUtilities::GetDebugInfoInternal(
			InstanceInfo,
			*Property,
			ValuePointer);

		for (auto& Child : InstanceInfo->Children)
		{
			Children.Add(FFastPropertyInstanceInfo{ Child->Property->ContainerPtrToValuePtr<void>(ValuePointer), Child });
		}
	}
}