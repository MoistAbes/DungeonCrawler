#include "PlayerCameraComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

UPlayerCameraComponent::UPlayerCameraComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    TargetArmLength = 400.0f;
    bIsZooming = false;
}

void UPlayerCameraComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!SpringArm || !Camera)
    {
        if (AActor* Owner = GetOwner())
        {
            if (!SpringArm)
            {
                SpringArm = Owner->FindComponentByClass<USpringArmComponent>();
            }
            if (!Camera)
            {
                Camera = Owner->FindComponentByClass<UCameraComponent>();
            }
        }
    }

    if (SpringArm)
    {
        TargetArmLength = SpringArm->TargetArmLength;
    }
}

void UPlayerCameraComponent::SetupCameraReferences(
    USpringArmComponent* InSpringArm,
    UCameraComponent* InCamera)
{
    SpringArm = InSpringArm;
    Camera = InCamera;

    if (SpringArm)
    {
        TargetArmLength = SpringArm->TargetArmLength;
    }
}

void UPlayerCameraComponent::HandleZoom(float InputValue)
{
    if (!SpringArm)
    {
        return;
    }

    const float ZoomDelta = InputValue * -1.0f;

    TargetArmLength = FMath::Clamp(
        TargetArmLength + (ZoomDelta * ZoomStep),
        MinZoomLength,
        MaxZoomLength);

    if (!bIsZooming)
    {
        bIsZooming = true;
        UpdateTickState();
    }
}

void UPlayerCameraComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsZooming || !SpringArm)
    {
        return;
    }

    const float CurrentLength = SpringArm->TargetArmLength;

    if (!FMath::IsNearlyEqual(CurrentLength, TargetArmLength, 0.1f))
    {
        SpringArm->TargetArmLength = FMath::FInterpTo(
            CurrentLength,
            TargetArmLength,
            DeltaTime,
            ZoomInterpSpeed);
    }
    else
    {
        SpringArm->TargetArmLength = TargetArmLength;
        bIsZooming = false;
        UpdateTickState();
    }
}

void UPlayerCameraComponent::UpdateTickState()
{
    const bool bShouldTick = bIsZooming;

    if (IsComponentTickEnabled() != bShouldTick)
    {
        SetComponentTickEnabled(bShouldTick);
    }
}
