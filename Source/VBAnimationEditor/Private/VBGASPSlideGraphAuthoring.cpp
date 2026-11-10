#include "VBGASPSlideGraphAuthoring.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPSlideLibrary.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

FString UVBGASPSlideGraphAuthoring::AdaptTrajectoryGraph(UAnimBlueprint* Child, UAnimBlueprint* Original)
{
	if (!IsInGameThread() || !Child || !Original || !Original->GeneratedClass
		|| !Child->ParentClass->IsChildOf(Original->GeneratedClass))
	{
		return TEXT("Slide child must inherit the original Mover ABP");
	}

	for (UEdGraph* Existing : Child->FunctionGraphs)
	{
		if (Existing && Existing->GetFName() == TEXT("Update_Trajectory"))
		{
			return TEXT("Slide trajectory override already exists; inspect before replacing it");
		}
	}

	UEdGraph* Source = nullptr;
	for (UEdGraph* Graph : Original->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == TEXT("Update_Trajectory"))
		{
			Source = Graph;
		}
	}

	if (!Source)
	{
		return TEXT("Original Update_Trajectory graph missing");
	}

	UFunction* Native = UVBGASPSlideLibrary::StaticClass()->FindFunctionByName(TEXT("GenerateSlideTrajectory"));
	if (!Native)
	{
		return TEXT("CMC trajectory adapter missing");
	}

	Child->Modify();
	UEdGraph* Graph = FEdGraphUtilities::CloneGraph(Source, Child);
	Graph->Rename(TEXT("Update_Trajectory"), Child);
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		Node->CreateNewGuid();
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			Entry->FunctionReference.SetExternalMember(TEXT("Update_Trajectory"), Original->GeneratedClass.Get());
		}
	}

	UK2Node_CallFunction* Predictor = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Call = Cast<UK2Node_CallFunction>(Node); Call && Call->FindPin(TEXT("InPredictor")))
		{
			Predictor = Call;
		}
	}

	if (!Predictor)
	{
		return TEXT("Original trajectory predictor call missing");
	}

	auto* Adapter = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Adapter, false, false);
	Adapter->CreateNewGuid();
	Adapter->SetFromFunction(Native);
	Adapter->AllocateDefaultPins();
	Adapter->NodePosX = Predictor->NodePosX;
	Adapter->NodePosY = Predictor->NodePosY;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	for (UEdGraphPin* Pin : Adapter->Pins)
	{
		if (Pin->PinName == TEXT("self") || Pin->PinName == TEXT("Instance"))
		{
			continue;
		}

		UEdGraphPin* Old = Predictor->FindPin(Pin->PinName, Pin->Direction);
		if (!Old || !Schema->ArePinTypesCompatible(Old->PinType, Pin->PinType))
		{
			return FString::Printf(TEXT("Trajectory adapter pin mismatch: %s"), *Pin->PinName.ToString());
		}

		Pin->DefaultValue = Old->DefaultValue;
		Pin->DefaultObject = Old->DefaultObject;
		Pin->DefaultTextValue = Old->DefaultTextValue;
		const TArray<UEdGraphPin*> Links = Old->LinkedTo;
		Old->BreakAllPinLinks();
		for (UEdGraphPin* Link : Links)
		{
			if (!Schema->TryCreateConnection(Pin, Link))
			{
				return TEXT("Trajectory adapter connection failed");
			}
		}
	}

	auto* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Self, false, false);
	Self->CreateNewGuid();
	Self->AllocateDefaultPins();
	if (!Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Adapter->FindPinChecked(TEXT("Instance"))))
	{
		return TEXT("Trajectory adapter self connection failed");
	}

	const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node->GetClass()->GetFName() != TEXT("K2Node_PropertyAccess"))
		{
			continue;
		}

		UEdGraphPin* Value = Node->FindPin(TEXT("Value"), EGPD_Output);
		if (!Value || Value->LinkedTo.Num() != 1 || Value->LinkedTo[0] != Adapter->FindPin(TEXT("InDeltaTime")))
		{
			return TEXT("Unexpected property access in original slide trajectory");
		}

		auto* Delta = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Delta, false, false);
		Delta->CreateNewGuid();
		Delta->SetFromFunction(UAnimInstance::StaticClass()->FindFunctionByName(TEXT("GetDeltaSeconds")));
		Delta->AllocateDefaultPins();
		Value->BreakAllPinLinks();
		if (!Schema->TryCreateConnection(Delta->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue),
			Adapter->FindPinChecked(TEXT("InDeltaTime")))
			|| !Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),
				Delta->FindPinChecked(UEdGraphSchema_K2::PN_Self)))
		{
			return TEXT("Slide delta time adapter connection failed");
		}

		FBlueprintEditorUtils::RemoveNode(Child, Node, true);
	}

	FBlueprintEditorUtils::RemoveNode(Child, Predictor, true);
	Child->FunctionGraphs.Add(Graph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Child);
	return FString();
}

FString UVBGASPSlideGraphAuthoring::InstallSlideLink(UAnimBlueprint* Main, UAnimBlueprint* Slide, FName LinkTag, FName ActiveVariable)
{
	if (!IsInGameThread() || !Main || !Slide || !Slide->GeneratedClass
		|| Main->TargetSkeleton != Slide->TargetSkeleton)
	{
		return TEXT("Main and slide skeletons must match");
	}

	if ((LinkTag != TEXT("GASPSlide") && LinkTag != TEXT("GASPUnarmed"))
		|| !FindFProperty<FBoolProperty>(Main->GeneratedClass, ActiveVariable))
	{
		return TEXT("Unexpected GASP link contract");
	}

	TArray<UEdGraph*> Graphs;
	Main->GetAllGraphs(Graphs);
	UAnimGraphNode_LinkedAnimGraph* Locomotion = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Linked = Cast<UAnimGraphNode_LinkedAnimGraph>(Node))
			{
				if (Linked->GetTag() == LinkTag)
				{
					return TEXT("GASP link already exists; inspect before replacing it");
				}

				if (Linked->GetTag() == TEXT("GASPLocomotion"))
				{
					Locomotion = Linked;
				}
			}
		}
	}

	UEdGraphPin* Source = Locomotion ? Locomotion->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
	if (!Source || Source->LinkedTo.IsEmpty())
	{
		return TEXT("Linked locomotion output has no consumer");
	}

	Main->Modify();
	UEdGraph* Graph = Locomotion->GetGraph();
	Graph->Modify();
	const TArray<UEdGraphPin*> Consumers = Source->LinkedTo;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UAnimGraphNode_LinkedAnimGraph* Linked = nullptr;
	UAnimGraphNode_BlendListByBool* Blend = nullptr;
	UK2Node_VariableGet* Read = nullptr;
	bool bInstalled = false;
	ON_SCOPE_EXIT
	{
		if (bInstalled)
		{
			return;
		}

		for (UEdGraphNode* Node : {static_cast<UEdGraphNode*>(Read), static_cast<UEdGraphNode*>(Blend), static_cast<UEdGraphNode*>(Linked)})
			if (Node)
			{
				FBlueprintEditorUtils::RemoveNode(Main, Node, true);
			}
		Source->BreakAllPinLinks();
		for (UEdGraphPin* Consumer : Consumers)
		{
			Schema->TryCreateConnection(Source, Consumer);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Main);
	};
	Linked = NewObject<UAnimGraphNode_LinkedAnimGraph>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Linked, false, false);
	Linked->CreateNewGuid();
	Linked->Node.InstanceClass = Slide->GeneratedClass;
	Linked->Node.bPropagateNotifiesToLinkedInstances = true;
	Linked->Node.bReceiveNotifiesFromLinkedInstances = false;
	Linked->SetTag(LinkTag);
	Linked->PostPlacedNewNode();
	Linked->AllocateDefaultPins();
	Linked->NodePosX = Locomotion->NodePosX;
	Linked->NodePosY = Locomotion->NodePosY + 250;
	Blend = NewObject<UAnimGraphNode_BlendListByBool>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Blend, false, false);
	Blend->CreateNewGuid();
	Blend->PostPlacedNewNode();
	Blend->AllocateDefaultPins();
	Blend->NodePosX = Locomotion->NodePosX + 400;
	Blend->NodePosY = Locomotion->NodePosY;
	const FEnumProperty* UpdateMode = FindFProperty<FEnumProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("ChildUpateMode"));
	if (!UpdateMode)
	{
		return TEXT("Blend child update mode missing");
	}

	UpdateMode->GetUnderlyingProperty()->SetIntPropertyValue(UpdateMode->ContainerPtrToValuePtr<void>(&Blend->Node),
		static_cast<int64>(EBlendListChildUpdateMode::ResetChildOnActivate));
	Read = NewObject<UK2Node_VariableGet>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Read, false, false);
	Read->CreateNewGuid();
	Read->VariableReference.SetSelfMember(ActiveVariable);
	Read->AllocateDefaultPins();
	Read->NodePosX = Blend->NodePosX;
	Read->NodePosY = Blend->NodePosY + 180;
	UEdGraphPin* Active = Blend->FindPin(TEXT("bActiveValue"));
	UEdGraphPin* Normal = Blend->FindPin(TEXT("BlendPose_1"));
	UEdGraphPin* Sliding = Blend->FindPin(TEXT("BlendPose_0"));
	UEdGraphPin* Output = Blend->FindPin(TEXT("Pose"), EGPD_Output);
	if (!Active || !Normal || !Sliding || !Output)
	{
		return TEXT("Slide blend pins missing");
	}

	Source->BreakAllPinLinks();
	bool bValid = Schema->TryCreateConnection(Source, Normal)
		&& Schema->TryCreateConnection(Linked->FindPinChecked(TEXT("Pose")), Sliding)
		&& Schema->TryCreateConnection(Read->FindPinChecked(ActiveVariable), Active);
	for (UEdGraphPin* Consumer : Consumers)
	{
		bValid &= Schema->TryCreateConnection(Output, Consumer);
	}

	bInstalled = bValid;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Main);
	return bValid ? FString() : TEXT("Slide pose routing failed");
}

FString UVBGASPSlideGraphAuthoring::AdaptNotifyRelay(UAnimBlueprint* Main)
{
	if (!IsInGameThread() || !Main || !Main->ParentClass->IsChildOf(UVBAnimInstance::StaticClass()))
	{
		return TEXT("Native VB animation blueprint required");
	}

	UFunction* Getter = UVBAnimInstance::StaticClass()->FindFunctionByName(TEXT("GetGASPNotifyInstance"));
	if (!Getter)
	{
		return TEXT("Active notify getter missing");
	}

	TArray<UK2Node_CallFunction*> Calls;
	for (const FName Name : {FName(TEXT("Get_Gait")), FName(TEXT("Set_NotifyTransition_ToLoop")),
		FName(TEXT("Set_NotifyTransition_ReTransition"))})
	{
		UK2Node_CallFunction* Receiver = nullptr;
		for (const FBPInterfaceDescription& Interface : Main->ImplementedInterfaces)
			for (UEdGraph* Graph : Interface.Graphs)
				if (Graph && Graph->GetFName() == Name)
					for (UEdGraphNode* Node : Graph->Nodes)
						if (auto* Call = Cast<UK2Node_CallFunction>(Node); Call &&
							(Call->FunctionReference.GetMemberName() == TEXT("GetGASPLocomotionInstance")
							|| Call->FunctionReference.GetMemberName() == TEXT("GetGASPNotifyInstance")))
						{
							Receiver = Call;
						}
		if (!Receiver)
		{
			return FString::Printf(TEXT("Original interface receiver missing: %s"), *Name.ToString());
		}

		Calls.Add(Receiver);
	}

	Main->Modify();
	for (UK2Node_CallFunction* Call : Calls)
	{
		Call->Modify();
		Call->SetFromFunction(Getter);
		Call->ReconstructNode();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Main);
	return FString();
}

AActor* UVBGASPSlideGraphAuthoring::CreateSlideValidationFloor(UObject* WorldContextObject, FVector Location)
{
	UWorld* World = Cast<UWorld>(WorldContextObject);
	if (!World && WorldContextObject)
	{
		World = WorldContextObject->GetWorld();
	}

	if (!IsInGameThread() || !World || World->WorldType != EWorldType::PIE)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Mesh)
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, Parameters);
	if (!Floor)
	{
		return nullptr;
	}

	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetActorScale3D(FVector(200.f, 200.f, 0.5f));
	return Floor;
}
