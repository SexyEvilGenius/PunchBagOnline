#include "MotionInterpolatorComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"

UMotionInterpolatorComponent::UMotionInterpolatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMotionInterpolatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	UWorld* World = GetWorld();
	const float currentSyncedTime = World->GetGameState()->GetServerWorldTimeSeconds();
	const AActor* Owner = GetOwner();
	AController* InstigatorController = IsValid(Owner) ? Owner->GetInstigatorController() : nullptr;
	APlayerController* LocalPlayerController = World->GetNetMode()==NM_DedicatedServer ? nullptr : UGameplayStatics::GetPlayerController(GetWorld(), 0);

	ENetRole OwnerRole = IsValid(Owner) ? Owner->GetLocalRole() : ROLE_None;
	ENetRole OwnerRemoteRole = IsValid(Owner) ? Owner->GetRemoteRole() : ROLE_None;
	USceneComponent* component = GetComponentToSync();
	UPrimitiveComponent* physComponent = Cast<UPrimitiveComponent>(component);

	if (IsValid(component))
	{
		bool hasMovementAuthority = false;
		if (currentSyncedTime <= AuthorityReleaseTime)
		{
			hasMovementAuthority = true;
		}
		else
		{
			if (IsValid(Owner))
			{
				hasMovementAuthority = OwnerRole == ROLE_Authority ? !Owner->HasNetOwner() || Owner->HasLocalNetOwner() : Owner->HasLocalNetOwner();
			}
		}
		if (hasMovementAuthority)
		{
			Snapshots.Empty();
			if ((SyncPeriod < KINDA_SMALL_NUMBER || (currentSyncedTime - LastSyncTime) > SyncPeriod))
			{
				FMotionSnapshot Snapshot;
				Snapshot.Timestamp = currentSyncedTime;
				Snapshot.Transform = component->GetComponentTransform();
				Snapshot.Velocity = component->GetComponentVelocity();
				if (IsValid(physComponent))
				{
					Snapshot.AngularVelocity = physComponent->GetPhysicsAngularVelocityInRadians();
				}

				ServerSendSnapshot(Snapshot, OwnerRole);
				LastSyncTime = currentSyncedTime;
			}
		}
		else if (!IsValid(physComponent) || SnapPeriod < KINDA_SMALL_NUMBER || (currentSyncedTime - LastSnapTime) > SnapPeriod)
		{
			FMotionSnapshot Snapshot;
			int OffBorder = 0;
			GetSnapshotAtTime(currentSyncedTime - NetworkDelay, UseExtrapolation, Snapshot, OffBorder);
			const float blendOutTime = AuthorityBlendOutFinishTime - currentSyncedTime;

			if (OffBorder == 0)
			{
				if (blendOutTime > 0.0f)
				{
					const float alpha = 1.0f - (blendOutTime / AuthorityBlendOutDuration);
					FMotionSnapshot NewSnapshot;
					NewSnapshot.Timestamp = currentSyncedTime;
					NewSnapshot.Transform = component->GetComponentTransform();
					NewSnapshot.Velocity = component->GetComponentVelocity();
					if (IsValid(physComponent))
					{
						NewSnapshot.AngularVelocity = physComponent->GetPhysicsAngularVelocityInRadians();
					}
					Snapshot = SimpleInterpolate(NewSnapshot, Snapshot, alpha);
				}

				component->SetWorldTransform(Snapshot.Transform);
				if (IsValid(physComponent))
				{
					physComponent->SetPhysicsLinearVelocity(Snapshot.Velocity);
					physComponent->SetPhysicsAngularVelocityInRadians(Snapshot.AngularVelocity);
				}
				LastSnapTime = currentSyncedTime;
			}
		}
	}
}

void UMotionInterpolatorComponent::SetComponentOverride(class USceneComponent* InComponentOverride)
{
	ComponentOverride = InComponentOverride;
}

void UMotionInterpolatorComponent::AddSnapshot(const FMotionSnapshot& Snapshot)
{
	if (Snapshots.Num() >= BufferSize)
	{
		Snapshots.RemoveAt(0, (Snapshots.Num() - BufferSize) + 1);
	}
	Snapshots.Add(Snapshot);
	OnSnapshotAdded.Broadcast(Snapshot);
}

void UMotionInterpolatorComponent::GetSnapshotAtTime(float TargetTime, bool CanExtrapolate, FMotionSnapshot& Result, int& OffBorder)
{
	OffBorder = 0;
	if (Snapshots.Num() <= 0)
	{
		OffBorder = -1;
		return;
	}
	else if (TargetTime <= Snapshots[0].Timestamp)
	{
		OffBorder = -1;
		Result = Snapshots[0];
		return;
	}
	else if (TargetTime >= Snapshots.Last().Timestamp)
	{
		if (!CanExtrapolate)
		{
			OffBorder = 1;
			Result = Snapshots.Last();
			return;
		}
	}

	FMotionSnapshot FirstSnapshot;
	FMotionSnapshot SecondSnapshot;
	for (size_t i = 0; i < Snapshots.Num(); i++)
	{
		if (Snapshots[i].Timestamp == TargetTime)
		{
			Result = Snapshots[i];
			return;
		}
		else if (i + 1 < Snapshots.Num() && Snapshots[i + 1].Timestamp > TargetTime)
		{
			FirstSnapshot = Snapshots[i];
			SecondSnapshot = Snapshots[i + 1];
			Result = Interpolate(FirstSnapshot, SecondSnapshot, TargetTime);
			return;
		}
		else if (i + 1 == Snapshots.Num())
		{
			FirstSnapshot = Snapshots[i];
			Result = Extrapolate(FirstSnapshot, TargetTime);
			return;
		}
	}
}

void UMotionInterpolatorComponent::ServerSendSnapshot_Implementation(const FMotionSnapshot& InSnapshot, ENetRole SenderRole)
{
	MulticastSendSnapshot(InSnapshot, SenderRole);
}

void UMotionInterpolatorComponent::MulticastSendSnapshot_Implementation(const FMotionSnapshot& InSnapshot, ENetRole SenderRole)
{
	if (SenderRole != GetOwnerRole())
	{
		AddSnapshot(InSnapshot);
	}
}

void UMotionInterpolatorComponent::ResetToNewestPredictedPose()
{
	USceneComponent* component = GetComponentToSync();
	UPrimitiveComponent* physComponent = Cast<UPrimitiveComponent>(component);
	const float currentSyncedTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();

	FMotionSnapshot Snapshot;
	int OffBorder = 0;
	GetSnapshotAtTime(currentSyncedTime+NetworkDelay, true, Snapshot, OffBorder);
	component->SetWorldTransform(Snapshot.Transform);
	if (IsValid(physComponent))
	{
		physComponent->SetPhysicsLinearVelocity(Snapshot.Velocity);
		physComponent->SetPhysicsAngularVelocityInRadians(Snapshot.AngularVelocity);
	}
}

void UMotionInterpolatorComponent::OvertakeMovementAuthority(float Duration, float BlendOutDuration)
{
	UWorld* World = GetWorld();
	AuthorityReleaseTime = World->GetGameState()->GetServerWorldTimeSeconds() + Duration;
	AuthorityBlendOutDuration = BlendOutDuration;
	AuthorityBlendOutFinishTime = World->GetGameState()->GetServerWorldTimeSeconds() + Duration + AuthorityBlendOutDuration;
}

FMotionSnapshot UMotionInterpolatorComponent::Interpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float TargetTime)
{
	float Alpha = UKismetMathLibrary::NormalizeToRange(TargetTime, FirstSnapshot.Timestamp, SecondSnapshot.Timestamp);

	float PredictionTime = TargetTime - FirstSnapshot.Timestamp;
	float ReversePredictionTime = SecondSnapshot.Timestamp - TargetTime;

	FVector ForwardPrediction = FirstSnapshot.Transform.GetLocation() + (FirstSnapshot.Velocity * PredictionTime);
	FVector BackwardPrediction = SecondSnapshot.Transform.GetLocation() + (FirstSnapshot.Velocity * ReversePredictionTime * -1);

	FMotionSnapshot Result;
	Result.Transform.SetLocation(FMath::Lerp(ForwardPrediction, BackwardPrediction, Alpha));
	Result.Transform.SetRotation(FMath::Lerp(FirstSnapshot.Transform.GetRotation(), SecondSnapshot.Transform.GetRotation(), FMath::InterpSinInOut<float>(0.f, 1.f, Alpha)));
	Result.Transform.SetScale3D(FMath::Lerp(FirstSnapshot.Transform.GetScale3D(), SecondSnapshot.Transform.GetScale3D(), Alpha));
	Result.Velocity = FMath::Lerp(FirstSnapshot.Velocity, SecondSnapshot.Velocity, Alpha);
	Result.AngularVelocity = FMath::Lerp(FirstSnapshot.AngularVelocity, SecondSnapshot.AngularVelocity, Alpha);
	Result.Timestamp = TargetTime;
	return Result;
}

FMotionSnapshot UMotionInterpolatorComponent::SimpleInterpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float Alpha)
{
	FMotionSnapshot Result;
	Result.Transform.SetLocation(FMath::Lerp(FirstSnapshot.Transform.GetLocation(), SecondSnapshot.Transform.GetLocation(), Alpha));
	Result.Transform.SetRotation(FMath::Lerp(FirstSnapshot.Transform.GetRotation(), SecondSnapshot.Transform.GetRotation(), FMath::InterpSinInOut<float>(0.f, 1.f, Alpha)));
	Result.Transform.SetScale3D(FMath::Lerp(FirstSnapshot.Transform.GetScale3D(), SecondSnapshot.Transform.GetScale3D(), Alpha));
	Result.Velocity = FMath::Lerp(FirstSnapshot.Velocity, SecondSnapshot.Velocity, Alpha);
	Result.AngularVelocity = FMath::Lerp(FirstSnapshot.AngularVelocity, SecondSnapshot.AngularVelocity, Alpha);
	Result.Timestamp = FMath::Lerp(FirstSnapshot.Timestamp, SecondSnapshot.Timestamp, Alpha);
	return Result;
}

FMotionSnapshot UMotionInterpolatorComponent::Extrapolate(const FMotionSnapshot& Snapshot, float TargetTime)
{
	float PredictionTime = TargetTime - Snapshot.Timestamp;

	FVector ForwardPrediction = Snapshot.Transform.GetLocation() + (Snapshot.Velocity * PredictionTime);

	FMotionSnapshot Result = Snapshot;
	Result.Transform.SetLocation(ForwardPrediction);
	Result.Timestamp = TargetTime;
	return Result;
}

USceneComponent* UMotionInterpolatorComponent::GetComponentToSync()
{
	if (IsValid(ComponentOverride))
	{
		return ComponentOverride;
	}
	if (!IsValid(ComponentToSync) || ComponentToSync->GetFName() != SyncedComponentName)
	{
		if (SyncedComponentName == NAME_None)
		{
			ComponentToSync = GetOwner()->GetRootComponent();
		}
		else
		{
			for (UActorComponent* Comp : GetOwner()->GetComponents())
			{
				if (Comp->GetFName() == SyncedComponentName)
				{
					if (UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(Comp))
					{
						if (AActor* ChildActor = ChildActorComp->GetChildActor())
						{
							ComponentToSync = ChildActor->GetRootComponent();
						}
					}
					else
					{
						ComponentToSync = Cast<USceneComponent>(Comp);
					}
					break;
				}
			}
		}
	}
	return ComponentToSync;
}