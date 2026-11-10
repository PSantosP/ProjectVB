// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class VBEditorTarget : TargetRules
{
	public VBEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		ExtraModuleNames.Add("VB");
		VBTarget.ApplySharedVBTargetSettings(this);
	}
}
