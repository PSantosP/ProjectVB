#include "VBGASPCMCGraphAuthoring.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionStateLibrary.h"
#include "AnimGraphNode_Base.h"
#include "Chooser.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"

namespace
{
	const FString NativeNodeMarker(TEXT("VBCMCNativeBlendInputs"));

	struct FPinMapping
	{
		FName Member;
		FName Input;
	};

	const FPinMapping PinMappings[] = {
		{TEXT("Anim"), TEXT("AnimationAsset")},
		{TEXT("StartTime"), TEXT("AnimationTime")},
		{TEXT("bLoop"), TEXT("bLoop")},
		{TEXT("bMirror"), TEXT("bMirrored")},
		{TEXT("BlendTime"), TEXT("BlendTime")},
		{TEXT("BlendProfile"), TEXT("BlendProfile")},
		{TEXT("BlendCurve"), TEXT("BlendOption")},
		{TEXT("BlendParameters"), TEXT("BlendParameters")}
	};

	struct FRequiredVariable
	{
		FName Name;
		FEdGraphPinType Type;
	};

	FEdGraphPinType StructPin(UScriptStruct* Struct)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = Struct;
		return Type;
	}

	FEdGraphPinType ChooserPin()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Object;
		Type.PinSubCategoryObject = UChooserTable::StaticClass();
		return Type;
	}

	bool SameVariableType(const FEdGraphPinType& A, const FEdGraphPinType& B)
	{
		return A.PinCategory == B.PinCategory && A.PinSubCategory == B.PinSubCategory
			&& A.PinSubCategoryObject == B.PinSubCategoryObject && A.ContainerType == B.ContainerType;
	}

	FString CheckVariable(UAnimBlueprint* Blueprint, const FRequiredVariable& Variable, bool& bExists)
	{
		bExists = false;
		// skeleton/generated class 가 컴파일을 기다리는 중이어도 blueprint 조상을 포함한다
		for (UBlueprint* Owner = Blueprint; Owner; Owner = Owner->ParentClass ? Cast<UBlueprint>(Owner->ParentClass->ClassGeneratedBy) : nullptr)
		{
			const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Owner, Variable.Name);
			if (Index != INDEX_NONE)
			{
				bExists = true;
				return SameVariableType(Owner->NewVariables[Index].VarType, Variable.Type) ? FString()
					: FString::Printf(TEXT("CMC variable %s has an incompatible authored type on %s"), *Variable.Name.ToString(),
						*Owner->GetName());
			}
		}
		// native 부모 property 는 NewVariables 에 없다
		if (const FProperty* Property = Blueprint->ParentClass ? FindFProperty<FProperty>(Blueprint->ParentClass, Variable.Name) : nullptr)
		{
			bExists = true;
			FEdGraphPinType Type;
			return GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, Type) && SameVariableType(Type, Variable.Type)
				? FString() : FString::Printf(TEXT("CMC variable %s conflicts with an inherited native property"),
					*Variable.Name.ToString());
		}

		return FString();
	}

	UEdGraphPin* StructInput(UK2Node_BreakStruct* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
				&& Pin->PinType.PinSubCategoryObject == FVBGASPCMCBlendInputs::StaticStruct())
			{
				return Pin;
			}
		return nullptr;
	}

	// BlendStackEditor 의존성을 늘리지 않으려고 runtime node 필드를 reflection 으로 읽는다
	FString CheckNodeSettings(UScriptStruct* Type, UMirrorDataTable* MirrorTable)
	{
		for (const TCHAR* Name : {TEXT("BlendParametersDeltaThreshold"), TEXT("MaxAnimationDeltaTime")})
		{
			const FNumericProperty* Property = FindFProperty<FNumericProperty>(Type, Name);
			if (!Property || !Property->IsFloatingPoint())
			{
				return FString::Printf(TEXT("BlendStack missing numeric setting %s"), Name);
			}
		}

		const FObjectPropertyBase* Mirror = FindFProperty<FObjectPropertyBase>(Type, TEXT("MirrorDataTable"));
		const FEnumProperty* Mode = FindFProperty<FEnumProperty>(Type, TEXT("BlendspaceUpdateMode"));
		if (!Mirror || !MirrorTable->IsA(Mirror->PropertyClass))
		{
			return TEXT("BlendStack MirrorDataTable type mismatch");
		}

		if (!Mode || !Mode->GetEnum() || Mode->GetEnum()->GetValueByNameString(TEXT("UpdateActiveOnly")) == INDEX_NONE)
		{
			return TEXT("BlendStack UpdateActiveOnly enum unavailable");
		}

		for (const TCHAR* Name : {TEXT("bForceBlendTo"), TEXT("bResetOnBecomingRelevant")})
			if (!FindFProperty<FBoolProperty>(Type, Name))
			{
				return FString::Printf(TEXT("BlendStack missing bool setting %s"), Name);
			}
		return FString();
	}

	void SetNodeSettings(UScriptStruct* Type, void* Data, UMirrorDataTable* MirrorTable)
	{
		const FNumericProperty* Threshold = FindFProperty<FNumericProperty>(Type, TEXT("BlendParametersDeltaThreshold"));
		Threshold->SetFloatingPointPropertyValue(Threshold->ContainerPtrToValuePtr<void>(Data), 2.0);
		const FNumericProperty* Delta = FindFProperty<FNumericProperty>(Type, TEXT("MaxAnimationDeltaTime"));
		Delta->SetFloatingPointPropertyValue(Delta->ContainerPtrToValuePtr<void>(Data), -1.0);
		const FEnumProperty* Mode = FindFProperty<FEnumProperty>(Type, TEXT("BlendspaceUpdateMode"));
		Mode->GetUnderlyingProperty()->SetIntPropertyValue(Mode->ContainerPtrToValuePtr<void>(Data),
			Mode->GetEnum()->GetValueByNameString(TEXT("UpdateActiveOnly")));
		FindFProperty<FObjectPropertyBase>(Type, TEXT("MirrorDataTable"))->SetObjectPropertyValue_InContainer(Data, MirrorTable);
		FindFProperty<FBoolProperty>(Type, TEXT("bForceBlendTo"))->SetPropertyValue_InContainer(Data, false);
		FindFProperty<FBoolProperty>(Type, TEXT("bResetOnBecomingRelevant"))->SetPropertyValue_InContainer(Data, true);
	}
}

FString UVBGASPCMCGraphAuthoring::InstallCMCBlendStackBindings(UAnimBlueprint* Base, UMirrorDataTable* MirrorTable)
{
	if (!IsInGameThread() || !Base || !MirrorTable)
	{
		return TEXT("CMC graph install requires game thread, blueprint and mirror table");
	}

	if (!Base->TargetSkeleton || MirrorTable->Skeleton != Base->TargetSkeleton)
	{
		return TEXT("CMC mirror table must use the blueprint target skeleton");
	}

	const FRequiredVariable Variables[] = {
		{TEXT("CMCState"), StructPin(FVBGASPCMCState::StaticStruct())},
		{TEXT("CMCBlendInputs"), StructPin(FVBGASPCMCBlendInputs::StaticStruct())},
		{TEXT("CMCSlopeSettings"), StructPin(FVBGSlopeSettings::StaticStruct())},
		{TEXT("CMCUnarmedChooser"), ChooserPin()},
		{TEXT("CMCWeaponChooser"), ChooserPin()}
	};
	TArray<int32> MissingVariables;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Variables); ++Index)
	{
		bool bExists;
		const FString Error = CheckVariable(Base, Variables[Index], bExists);
		if (!Error.IsEmpty())
		{
			return Error;
		}

		if (!bExists)
		{
			MissingVariables.Add(Index);
		}
	}

	UAnimGraphNode_Base* Stack = nullptr;
	TArray<UEdGraph*> Graphs;
	Base->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Candidate = Cast<UAnimGraphNode_Base>(Node); Candidate && Candidate->GetTag() == TEXT("State Machine Blend Stack"))
			{
				if (Stack)
				{
					return TEXT("CMC graph contains multiple State Machine Blend Stack tags");
				}

				Stack = Candidate;
			}
		}
	}

	if (!Stack || Stack->GetClass()->GetFName() != TEXT("AnimGraphNode_BlendStack"))
	{
		return TEXT("CMC State Machine Blend Stack node is missing or wrong type");
	}

	const FStructProperty* NodeProperty = FindFProperty<FStructProperty>(Stack->GetClass(), TEXT("Node"));
	if (!NodeProperty)
	{
		return TEXT("BlendStack runtime Node struct is missing");
	}

	FString Error = CheckNodeSettings(NodeProperty->Struct, MirrorTable);
	if (!Error.IsEmpty())
	{
		return Error;
	}

	UFunction* Function = UVBGASPLocomotionStateLibrary::StaticClass()->FindFunctionByName(TEXT("GetCMCBlendInputs"));
	if (!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
	{
		return TEXT("Native GetCMCBlendInputs pure function is unavailable");
	}

	for (const FPinMapping& Mapping : PinMappings)
	{
		if (!Stack->ShowPinForProperties.ContainsByPredicate([&](const FOptionalPinFromProperty& Pin) { return Pin.PropertyName == Mapping.Input; }))
			return FString::Printf(TEXT("BlendStack optional pin %s is unavailable"), *Mapping.Input.ToString());
	}

	Base->Modify();
	UEdGraph* Graph = Stack->GetGraph();
	Graph->Modify();
	Stack->Modify();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	// 새 GUID 로 만든다 - 옛 node identity 가 든 직렬화 graph 를 복제하지 않는다
	auto* Call = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Call, false, false); Call->CreateNewGuid(); Call->SetFromFunction(Function); Call->AllocateDefaultPins();
	auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Self, false, false); Self->CreateNewGuid(); Self->AllocateDefaultPins();
	auto* Break = NewObject<UK2Node_BreakStruct>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Break, false, false); Break->CreateNewGuid(); Break->StructType = FVBGASPCMCBlendInputs::StaticStruct();
	Break->bMadeAfterOverridePinRemoval = true;
	Break->AllocateDefaultPins();
	for (FOptionalPinFromProperty& Pin : Break->ShowPinForProperties)
	{
		Pin.bShowPin = true;
	}

	Break->ReconstructNode();
	auto RemoveNewNodes = [&]()
	{
		for (UEdGraphNode* Node : {static_cast<UEdGraphNode*>(Call), static_cast<UEdGraphNode*>(Self), static_cast<UEdGraphNode*>(Break)})
			FBlueprintEditorUtils::RemoveNode(Base, Node, true);
	};
	UEdGraphPin* SelfPin = Self->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* InstancePin = Call->FindPin(TEXT("Instance"));
	UEdGraphPin* ReturnPin = Call->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* BreakInput = StructInput(Break);
	if (!SelfPin || !InstancePin || !ReturnPin || !BreakInput
		|| !Schema->TryCreateConnection(SelfPin, InstancePin) || !Schema->TryCreateConnection(ReturnPin, BreakInput))
	{
		RemoveNewNodes();
		return TEXT("Failed to connect native CMC blend input call");
	}

	for (const FPinMapping& Mapping : PinMappings)
	{
		const UEdGraphPin* Member = Break->FindPin(Mapping.Member);
		const FProperty* Property = FindFProperty<FProperty>(NodeProperty->Struct, Mapping.Input);
		FEdGraphPinType InputType;
		if (!Member || !Property || !Schema->ConvertPropertyToPinType(Property, InputType)
			|| !Schema->ArePinTypesCompatible(Member->PinType, InputType))
		{
			RemoveNewNodes();
			return FString::Printf(TEXT("CMC native member %s cannot feed BlendStack %s"), *Mapping.Member.ToString(),
				*Mapping.Input.ToString());
		}
	}

	for (int32 Index : MissingVariables)
	{
		if (!FBlueprintEditorUtils::AddMemberVariable(Base, Variables[Index].Name, Variables[Index].Type))
		{
			RemoveNewNodes();
			return FString::Printf(TEXT("Unable to create CMC variable %s"), *Variables[Index].Name.ToString());
		}
	}

	// 이 재생 필드의 주인은 native 출력 하나다 - 옛 property access binding 이 남으면 컴파일 때 graph 연결을 조용히 이긴다
	for (const FPinMapping& Mapping : PinMappings)
	{
		Stack->RemoveBindings(Mapping.Input);
		for (FOptionalPinFromProperty& Pin : Stack->ShowPinForProperties)
		{
			if (Pin.PropertyName == Mapping.Input)
			{
				Pin.bShowPin = true;
			}
		}
	}

	for (const TCHAR* Name : {TEXT("bForceBlendTo"), TEXT("MirrorDataTable"), TEXT("MaxAnimationDeltaTime")})
	{
		Stack->RemoveBindings(Name);
		for (FOptionalPinFromProperty& Pin : Stack->ShowPinForProperties)
		{
			if (Pin.PropertyName == Name)
			{
				Pin.bShowPin = false;
			}
		}

		if (UEdGraphPin* Pin = Stack->FindPin(Name))
		{
			Pin->BreakAllPinLinks();
		}
	}

	SetNodeSettings(NodeProperty->Struct, NodeProperty->ContainerPtrToValuePtr<void>(Stack), MirrorTable);
	Stack->ReconstructNode();
	for (const FPinMapping& Mapping : PinMappings)
	{
		UEdGraphPin* Input = Stack->FindPin(Mapping.Input);
		if (!Input)
		{
			RemoveNewNodes();
			return FString::Printf(TEXT("BlendStack pin %s was not exposed after reconstruction"), *Mapping.Input.ToString());
		}

		Input->BreakAllPinLinks();
		if (!Schema->TryCreateConnection(Break->FindPin(Mapping.Member), Input))
		{
			RemoveNewNodes();
			return FString::Printf(TEXT("Failed to wire CMC member %s"), *Mapping.Member.ToString());
		}
	}
	// 이 installer 가 만든 노드만 바꾸고 원래 UDS 소비자, 도우미는 남긴다
	const TArray<TObjectPtr<UEdGraphNode>> Existing = Graph->Nodes;
	for (UEdGraphNode* Node : Existing)
	{
		if (Node && Node != Call && Node != Self && Node != Break && Node->NodeComment == NativeNodeMarker)
		{
			FBlueprintEditorUtils::RemoveNode(Base, Node, true);
		}
	}

	Call->NodeComment = Self->NodeComment = Break->NodeComment = NativeNodeMarker;
	Call->NodePosX = Stack->NodePosX - 700; Call->NodePosY = Stack->NodePosY;
	Self->NodePosX = Call->NodePosX - 220; Self->NodePosY = Call->NodePosY;
	Break->NodePosX = Stack->NodePosX - 370; Break->NodePosY = Stack->NodePosY;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Base);
	return FString();
}
