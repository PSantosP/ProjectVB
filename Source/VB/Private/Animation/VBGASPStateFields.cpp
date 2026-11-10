#include "VBGASPStateFields.h"

#include "Animation/AnimationAsset.h"

DEFINE_LOG_CATEGORY(LogVBGASPState);

namespace VBGASP
{
	bool WriteNames(const FStateView& View, FName Name, const TArray<FName>& Names)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(View.Find(Name));
		if (!Property || !CastField<FNameProperty>(Property->Inner))
		{
			return false;
		}

		*Property->ContainerPtrToValuePtr<TArray<FName>>(View.Data) = Names;
		return true;
	}

	bool WriteBool(const FStateView& State, FName Name, bool Value)
	{
		if (const FBoolProperty* Property = CastField<FBoolProperty>(State.Find(Name)))
		{
			Property->SetPropertyValue_InContainer(State.Data, Value);
			return true;
		}

		return false;
	}

	bool WriteNumber(const FStateView& State, FName Name, double Value)
	{
		if (const FNumericProperty* Property = CastField<FNumericProperty>(State.Find(Name)))
		{
			void* Data = Property->ContainerPtrToValuePtr<void>(State.Data);
			if (Property->IsFloatingPoint())
			{
				Property->SetFloatingPointPropertyValue(Data, Value);
			}
			else
			{
				Property->SetIntPropertyValue(Data, static_cast<int64>(Value));
			}

			return true;
		}

		return false;
	}

	bool WriteObject(const FStateView& State, FName Name, UObject* Value)
	{
		if (const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(State.Find(Name));
			Property && (!Value || Value->IsA(Property->PropertyClass)))
		{
			Property->SetObjectPropertyValue_InContainer(State.Data, Value);
			return true;
		}

		return false;
	}

	bool CopyField(const FStateView& Destination, FName DestinationName, const FStateView& Source, FName SourceName)
	{
		const FProperty* To = Destination.Find(DestinationName);
		const FProperty* From = Source.Find(SourceName);
		if (!To || !From || !To->SameType(From))
		{
			return false;
		}

		To->CopyCompleteValue(To->ContainerPtrToValuePtr<void>(Destination.Data), From->ContainerPtrToValuePtr<void>(Source.Data));
		return true;
	}

	FName ReadName(const FStateView& State, FName Name)
	{
		const FNameProperty* Property = CastField<FNameProperty>(State.Find(Name));
		return Property ? Property->GetPropertyValue_InContainer(State.Data) : NAME_None;
	}

	bool StoreValidAnimations(const FStateView& State, const TArray<UAnimationAsset*>& Animations)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(State.Find(TEXT("ValidAnims")));
		const FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
		if (!Property || !Inner)
		{
			return false;
		}

		FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(State.Data));
		Array.Resize(Animations.Num());
		for (int32 Index = 0; Index < Animations.Num(); ++Index)
		{
			Inner->SetObjectPropertyValue(Array.GetRawPtr(Index), Animations[Index]);
		}

		return true;
	}

	FName CMCStateName(const FStateView& View)
	{
		const auto* Property = CastField<FByteProperty>(View.Find(TEXT("StateMachineState")));
		return Property && Property->Enum ? FName(Property->Enum->GetAuthoredNameStringByValue(
			Property->GetPropertyValue_InContainer(View.Data))) : NAME_None;
	}
}
