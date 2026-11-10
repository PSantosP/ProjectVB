// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBRagdollPhysicsAuthoring.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

FString UVBRagdollPhysicsAuthoring::DescribeConstraints(UPhysicsAsset* Asset)
{
	FString Result;
	if (!Asset)
	{
		return Result;
	}
	for (const UPhysicsConstraintTemplate* Joint : Asset->ConstraintSetup)
	{
		if (Joint)
		{
			Result += Joint->GetName() + TEXT("\n");
			FConstraintInstance::StaticStruct()->ExportText(Result, &Joint->DefaultInstance, nullptr,
				nullptr, PPF_None, nullptr);
			Result += TEXT("\n");
		}
	}
	return Result;
}

bool UVBRagdollPhysicsAuthoring::StoreConstraintProfile(UPhysicsAsset* Asset, FName ProfileName)
{
	if (!Asset || ProfileName.IsNone() || Asset->ConstraintSetup.IsEmpty())
	{
		return false;
	}
	for (const UPhysicsConstraintTemplate* Joint : Asset->ConstraintSetup)
	{
		if (!Joint)
		{
			return false;
		}
	}
	Asset->Modify();
	for (UPhysicsConstraintTemplate* Joint : Asset->ConstraintSetup)
	{
		Joint->Modify();
		Joint->RemoveConstraintProfile(ProfileName);
		Joint->AddConstraintProfile(ProfileName);
	}
	Asset->ConstraintProfiles.AddUnique(ProfileName);
	Asset->MarkPackageDirty();
	return true;
}
