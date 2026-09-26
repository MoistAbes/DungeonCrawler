#pragma once

#include "CoreMinimal.h"
#include "MyProject/Environment/Elements/Enums/ElementEnums.h"
#include "MyProject/Shared/Enums/PhysicalMaterialEnums.h"
#include "MyProject/Environment/Elements/Data/StatusEffectTypes.h"
#include "SurfaceGridTypes.generated.h"

/**
 * Zorientowany kierunek powierzchni ściany lub podłogi.
 * Zapewnia niezależność przeciwnych stron ściany (rozbryzg z frontu nie przenika na tył).
 */
UENUM(BlueprintType)
enum class ESurfaceFaceDirection : uint8
{
	Up = 0    UMETA(DisplayName = "Up (+Z, Floor)"),
	Down = 1  UMETA(DisplayName = "Down (-Z, Ceiling)"),
	North = 2 UMETA(DisplayName = "North (+X)"),
	South = 3 UMETA(DisplayName = "South (-X)"),
	East = 4  UMETA(DisplayName = "East (+Y)"),
	West = 5  UMETA(DisplayName = "West (-Y)")
};

namespace SurfaceGridUtils
{
	/** Przelicza wektor normalny powierzchni na dominujący kierunek ESurfaceFaceDirection */
	FORCEINLINE ESurfaceFaceDirection NormalToFaceDirection(const FVector& Normal)
	{
		const float AbsX = FMath::Abs(Normal.X);
		const float AbsY = FMath::Abs(Normal.Y);
		const float AbsZ = FMath::Abs(Normal.Z);

		if (AbsZ >= AbsX && AbsZ >= AbsY)
		{
			return (Normal.Z >= 0.0f) ? ESurfaceFaceDirection::Up : ESurfaceFaceDirection::Down;
		}
		if (AbsX >= AbsY)
		{
			return (Normal.X >= 0.0f) ? ESurfaceFaceDirection::North : ESurfaceFaceDirection::South;
		}
		return (Normal.Y >= 0.0f) ? ESurfaceFaceDirection::East : ESurfaceFaceDirection::West;
	}

	/** Zwraca wektor jednostkowy przypisany do danego kierunku */
	FORCEINLINE FVector FaceDirectionToNormal(ESurfaceFaceDirection Face)
	{
		switch (Face)
		{
		case ESurfaceFaceDirection::Up:    return FVector(0.0f, 0.0f, 1.0f);
		case ESurfaceFaceDirection::Down:  return FVector(0.0f, 0.0f, -1.0f);
		case ESurfaceFaceDirection::North: return FVector(1.0f, 0.0f, 0.0f);
		case ESurfaceFaceDirection::South: return FVector(-1.0f, 0.0f, 0.0f);
		case ESurfaceFaceDirection::East:  return FVector(0.0f, 1.0f, 0.0f);
		case ESurfaceFaceDirection::West:  return FVector(0.0f, -1.0f, 0.0f);
		default:                           return FVector(0.0f, 0.0f, 1.0f);
		}
	}
}

/**
 * Klucz dyskretnej komórki powierzchniowej w przestrzeni świata 3D.
 * Identyfikuje pozycję w siatce oraz stronę fundamentu.
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FSurfaceCellCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid")
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid")
	int32 Z = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid")
	ESurfaceFaceDirection Face = ESurfaceFaceDirection::Up;

	FSurfaceCellCoord() = default;

	FSurfaceCellCoord(int32 InX, int32 InY, int32 InZ, ESurfaceFaceDirection InFace)
		: X(InX), Y(InY), Z(InZ), Face(InFace)
	{
	}

	/** Wylicza współrzędne komórki z pozycji świata, wektora normalnego i rozmiaru siatki */
	static FSurfaceCellCoord FromWorldLocation(const FVector& WorldLocation, const FVector& Normal, float CellSize)
	{
		const float SafeCellSize = FMath::Max(1.0f, CellSize);
		const ESurfaceFaceDirection FaceDir = SurfaceGridUtils::NormalToFaceDirection(Normal);
		const FVector SurfaceNormal = SurfaceGridUtils::FaceDirectionToNormal(FaceDir);
		// Przesuwamy punkt o niewielki margines w głąb komórki (wzdłuż jej normalnej),
		// co eliminuje błędy zaokrągleń zmiennoprzecinkowych na granicy siatki (np. Z=-0.0001 dające Floor=-1 zamiast 0).
		const FVector BiasedLocation = WorldLocation + SurfaceNormal * 2.0f;
		return FSurfaceCellCoord(
			FMath::FloorToInt(BiasedLocation.X / SafeCellSize),
			FMath::FloorToInt(BiasedLocation.Y / SafeCellSize),
			FMath::FloorToInt(BiasedLocation.Z / SafeCellSize),
			FaceDir
		);
	}

	/** Wylicza środek geometryczny komórki w przestrzeni świata */
	FVector ToWorldLocation(float CellSize) const
	{
		return FVector(
			(static_cast<float>(X) + 0.5f) * CellSize,
			(static_cast<float>(Y) + 0.5f) * CellSize,
			(static_cast<float>(Z) + 0.5f) * CellSize
		);
	}

	/** Zwraca 4 ortogonalne komórki sąsiadujące na tej samej płaszczyźnie (Face) */
	void GetCoplanarNeighbors(TArray<FSurfaceCellCoord>& OutNeighbors) const
	{
		OutNeighbors.Reset(4);
		switch (Face)
		{
		case ESurfaceFaceDirection::Up:
		case ESurfaceFaceDirection::Down:
			// Płaszczyzna XY (Podłoga / Sufit)
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, Face));
			break;

		case ESurfaceFaceDirection::North:
		case ESurfaceFaceDirection::South:
			// Płaszczyzna YZ (Ściana Północna / Południowa)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, Face));
			break;

		case ESurfaceFaceDirection::East:
		case ESurfaceFaceDirection::West:
			// Płaszczyzna XZ (Ściana Wschodnia / Zachodnia)
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, Face));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, Face));
			break;
		}
	}

	/** Alias ścieżki rozprzestrzeniania */
	using FSpreadPath = struct FSurfaceSpreadPath;

	/** Zwraca 4 ortogonalne ścieżki rozprzestrzeniania z hierarchią: najpierw współpłaszczyznowa, potem narożna */
	void GetDirectionalSpreadPaths(TArray<struct FSurfaceSpreadPath, TInlineAllocator<4>>& OutPaths) const;


	/** Zwraca wszystkich sąsiadów: 4 współpłaszczyznowe oraz sąsiadów na krawędziach 90° (podłoga <-> ściany <-> sufit) */
	void GetAdjacentNeighbors(TArray<FSurfaceCellCoord>& OutNeighbors) const
	{
		OutNeighbors.Reset();
		OutNeighbors.Reserve(20);

		// 1. Zawsze dodajemy 4 sąsiadów leżących na tej samej płaszczyźnie
		GetCoplanarNeighbors(OutNeighbors);

		// 2. Dodajemy kandydatów z prostopadłych ścian/podłóg łączących się na krawędziach 90°
		switch (Face)
		{
		case ESurfaceFaceDirection::Up:
			// Krawędź +X (ściana South)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z + 1, ESurfaceFaceDirection::South));

			// Krawędź -X (ściana North)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z + 1, ESurfaceFaceDirection::North));

			// Krawędź +Y (ściana West)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z + 1, ESurfaceFaceDirection::West));

			// Krawędź -Y (ściana East)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z + 1, ESurfaceFaceDirection::East));
			break;

		case ESurfaceFaceDirection::Down:
			// Krawędź +X (ściana South)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z - 1, ESurfaceFaceDirection::South));

			// Krawędź -X (ściana North)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z - 1, ESurfaceFaceDirection::North));

			// Krawędź +Y (ściana West)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z - 1, ESurfaceFaceDirection::West));

			// Krawędź -Y (ściana East)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z - 1, ESurfaceFaceDirection::East));
			break;

		case ESurfaceFaceDirection::North:
			// Krawędź -Z (podłoga Up)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z - 1, ESurfaceFaceDirection::Up));

			// Krawędź +Z (sufit Down)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z + 1, ESurfaceFaceDirection::Down));

			// Krawędź +Y (ściana West)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y + 1, Z, ESurfaceFaceDirection::West));

			// Krawędź -Y (ściana East)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y - 1, Z, ESurfaceFaceDirection::East));
			break;

		case ESurfaceFaceDirection::South:
			// Krawędź -Z (podłoga Up)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z - 1, ESurfaceFaceDirection::Up));

			// Krawędź +Z (sufit Down)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z + 1, ESurfaceFaceDirection::Down));

			// Krawędź +Y (ściana West)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::West));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y + 1, Z, ESurfaceFaceDirection::West));

			// Krawędź -Y (ściana East)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::East));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y - 1, Z, ESurfaceFaceDirection::East));
			break;

		case ESurfaceFaceDirection::East:
			// Krawędź -Z (podłoga Up)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z - 1, ESurfaceFaceDirection::Up));

			// Krawędź +Z (sufit Down)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z + 1, ESurfaceFaceDirection::Down));

			// Krawędź +X (ściana South)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y + 1, Z, ESurfaceFaceDirection::South));

			// Krawędź -X (ściana North)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y + 1, Z, ESurfaceFaceDirection::North));
			break;

		case ESurfaceFaceDirection::West:
			// Krawędź -Z (podłoga Up)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z - 1, ESurfaceFaceDirection::Up));

			// Krawędź +Z (sufit Down)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z + 1, ESurfaceFaceDirection::Down));

			// Krawędź +X (ściana South)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::South));
			OutNeighbors.Add(FSurfaceCellCoord(X + 1, Y - 1, Z, ESurfaceFaceDirection::South));

			// Krawędź -X (ściana North)
			OutNeighbors.Add(FSurfaceCellCoord(X, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::North));
			OutNeighbors.Add(FSurfaceCellCoord(X - 1, Y - 1, Z, ESurfaceFaceDirection::North));
			break;
		}
	}

	bool operator==(const FSurfaceCellCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z && Face == Other.Face;
	}

	friend uint32 GetTypeHash(const FSurfaceCellCoord& Coord)
	{
		uint32 Hash = GetTypeHash(Coord.X);
		Hash = HashCombine(Hash, GetTypeHash(Coord.Y));
		Hash = HashCombine(Hash, GetTypeHash(Coord.Z));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Coord.Face)));
		return Hash;
	}
};

/**
 * Pojedyncza ortogonalna ścieżka rozprzestrzeniania z hierarchią: najpierw płaszczyzna, potem narożnik.
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FSurfaceSpreadPath
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	FSurfaceCellCoord Coplanar;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	FSurfaceCellCoord Corner;

	FSurfaceSpreadPath() = default;

	FSurfaceSpreadPath(const FSurfaceCellCoord& InCoplanar, const FSurfaceCellCoord& InCorner)
		: Coplanar(InCoplanar), Corner(InCorner)
	{
	}
};

FORCEINLINE void FSurfaceCellCoord::GetDirectionalSpreadPaths(TArray<FSurfaceSpreadPath, TInlineAllocator<4>>& OutPaths) const
{
	OutPaths.Reset(4);
	switch (Face)
	{
	case ESurfaceFaceDirection::Up:
		OutPaths.Emplace(FSurfaceCellCoord(X + 1, Y, Z, Face), FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
		OutPaths.Emplace(FSurfaceCellCoord(X - 1, Y, Z, Face), FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y + 1, Z, Face), FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y - 1, Z, Face), FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
		break;

	case ESurfaceFaceDirection::Down:
		OutPaths.Emplace(FSurfaceCellCoord(X + 1, Y, Z, Face), FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
		OutPaths.Emplace(FSurfaceCellCoord(X - 1, Y, Z, Face), FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y + 1, Z, Face), FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y - 1, Z, Face), FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
		break;

	case ESurfaceFaceDirection::North:
		OutPaths.Emplace(FSurfaceCellCoord(X, Y + 1, Z, Face), FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y - 1, Z, Face), FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z + 1, Face), FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z - 1, Face), FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
		break;

	case ESurfaceFaceDirection::South:
		OutPaths.Emplace(FSurfaceCellCoord(X, Y + 1, Z, Face), FSurfaceCellCoord(X, Y + 1, Z, ESurfaceFaceDirection::West));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y - 1, Z, Face), FSurfaceCellCoord(X, Y - 1, Z, ESurfaceFaceDirection::East));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z + 1, Face), FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z - 1, Face), FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
		break;

	case ESurfaceFaceDirection::East:
		OutPaths.Emplace(FSurfaceCellCoord(X + 1, Y, Z, Face), FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
		OutPaths.Emplace(FSurfaceCellCoord(X - 1, Y, Z, Face), FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z + 1, Face), FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z - 1, Face), FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
		break;

	case ESurfaceFaceDirection::West:
		OutPaths.Emplace(FSurfaceCellCoord(X + 1, Y, Z, Face), FSurfaceCellCoord(X + 1, Y, Z, ESurfaceFaceDirection::South));
		OutPaths.Emplace(FSurfaceCellCoord(X - 1, Y, Z, Face), FSurfaceCellCoord(X - 1, Y, Z, ESurfaceFaceDirection::North));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z + 1, Face), FSurfaceCellCoord(X, Y, Z + 1, ESurfaceFaceDirection::Down));
		OutPaths.Emplace(FSurfaceCellCoord(X, Y, Z - 1, Face), FSurfaceCellCoord(X, Y, Z - 1, ESurfaceFaceDirection::Up));
		break;
	}
}


/**
 * Pojedynczy aktywny status żywiołowy na komórce siatki wraz z czasem wygaśnięcia i instigatorem.
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FSurfaceCellStatusEntry
{
	GENERATED_BODY()

	/** Typ aktywnego statusu żywiołowego na komórce (Burning, Wet, Oiled, Electrified itd.) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	EStatusEffectType Status = EStatusEffectType::None;

	/** Poziom/Tier nałożonego statusu (0 = bazowy, 1 = silny, 2 = piekielny) decydujący o DPS i parametrach */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	uint8 Tier = 0;

	/** Czas serwera (GetTimeSeconds), w którym ten konkretny status wygasa (wartość <= 0.0f oznacza efekt permanentny) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	float ServerEndTime = 0.0f;

	/** Aktor, który nałożył ten status */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	TWeakObjectPtr<AActor> Instigator = nullptr;

	FSurfaceCellStatusEntry() = default;

	FSurfaceCellStatusEntry(EStatusEffectType InStatus, float InEndTime, AActor* InInstigator = nullptr, uint8 InTier = 0)
		: Status(InStatus), Tier(InTier), ServerEndTime(InEndTime), Instigator(InInstigator)
	{
	}

	FORCEINLINE bool IsPermanent() const { return ServerEndTime <= 0.0f; }
	FORCEINLINE void SetPermanent() { ServerEndTime = 0.0f; }
	FORCEINLINE bool IsExpired(float CurrentTime) const { return !IsPermanent() && CurrentTime >= ServerEndTime; }
	FORCEINLINE float GetRemainingDuration(float CurrentTime) const 
	{ 
		return IsPermanent() ? 999999.0f : FMath::Max(0.0f, ServerEndTime - CurrentTime); 
	}

	bool operator==(const FSurfaceCellStatusEntry& Other) const
	{
		return Status == Other.Status;
	}
};

/**
 * Dane pojedynczej aktywnej komórki powierzchniowej w podsystemie.
 * Zapewnia pełną skalowalność i wielostatusowość (np. [Wet, Electrified])
 * z zerowym narzutem alokacji pamięci dzięki TInlineAllocator<2>.
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FSurfaceCellData
{
	GENERATED_BODY()

	/** Lista aktywnych statusów na komórce (max 2 trzymane bezpośrednio w strukturze inline) */
	TArray<FSurfaceCellStatusEntry, TInlineAllocator<2>> ActiveStatuses;

	/** Materiał fizyczny architektury podłoża (np. Stone, Wood, Metal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom|SurfaceGrid")
	EPhysicalMaterialType SurfaceMaterial = EPhysicalMaterialType::Stone;

	/** Aktor fundamentu/struktury lochu, na którym znajduje się ta komórka */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	TWeakObjectPtr<AActor> SurfaceActor = nullptr;

	/** Czas serwera (GetTimeSeconds), w którym komórka spróbuje rozprzestrzenić stały ogień na sąsiadów */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	float NextFuelSpreadTime = 0.0f;

	bool IsEmpty() const
	{
		return ActiveStatuses.Num() == 0;
	}

	bool HasStatus(EStatusEffectType InStatus) const
	{
		for (const FSurfaceCellStatusEntry& Entry : ActiveStatuses)
		{
			if (Entry.Status == InStatus)
			{
				return true;
			}
		}
		return false;
	}

	FSurfaceCellStatusEntry* FindStatus(EStatusEffectType InStatus)
	{
		for (FSurfaceCellStatusEntry& Entry : ActiveStatuses)
		{
			if (Entry.Status == InStatus)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	const FSurfaceCellStatusEntry* FindStatus(EStatusEffectType InStatus) const
	{
		for (const FSurfaceCellStatusEntry& Entry : ActiveStatuses)
		{
			if (Entry.Status == InStatus)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	void RemoveStatus(EStatusEffectType InStatus)
	{
		for (int32 i = ActiveStatuses.Num() - 1; i >= 0; --i)
		{
			if (ActiveStatuses[i].Status == InStatus)
			{
				ActiveStatuses.RemoveAt(i);
			}
		}
	}

	TArray<EStatusEffectType> GetStatusTypes() const
	{
		TArray<EStatusEffectType> Types;
		Types.Reserve(ActiveStatuses.Num());
		for (const FSurfaceCellStatusEntry& Entry : ActiveStatuses)
		{
			Types.Add(Entry.Status);
		}
		return Types;
	}

	/** Zwraca dominujący status do celów wizualnych / debugowych */
	EStatusEffectType GetDominantStatus() const
	{
		if (ActiveStatuses.Num() == 0)
		{
			return EStatusEffectType::None;
		}
		// Jeśli komórka ma prąd i ciecz, prąd jest najbardziej widocznym efektem
		if (HasStatus(EStatusEffectType::Electrified))
		{
			return EStatusEffectType::Electrified;
		}
		if (HasStatus(EStatusEffectType::Burning))
		{
			return EStatusEffectType::Burning;
		}
		return ActiveStatuses[0].Status;
	}

	/** Zwraca aktora odpowiedzialnego za dominujący status */
	AActor* GetDominantInstigator() const
	{
		const EStatusEffectType Dominant = GetDominantStatus();
		if (const FSurfaceCellStatusEntry* Entry = FindStatus(Dominant))
		{
			return Entry->Instigator.Get();
		}
		return ActiveStatuses.Num() > 0 ? ActiveStatuses[0].Instigator.Get() : nullptr;
	}
};

/**
 * Raport z przejścia stanu komórki powierzchniowej wyliczany przez silnik praw żywiołów (UElementalReactionRules).
 */
USTRUCT(BlueprintType)
struct MYPROJECT_API FSurfaceCellTransitionResult
{
	GENERATED_BODY()

	/** Czy status został zaakceptowany/zaaplikowany przez zasady chemii */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	bool bAccepted = false;

	/** Czy stan komórki uległ jakiejkolwiek modyfikacji (do wysłania delegatu OnSurfaceCellChanged) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	bool bStateModified = false;

	/** Czy komórka po tej operacji stała się całkowicie pusta (np. ugaszenie, neutralizacja) i powinna zostać usunięta z siatki */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	bool bCellBecameEmpty = false;

	/** Wynik reakcji chemicznej (jeśli zaszła), np. do zadania bonus instant damage / wybuchu */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom|SurfaceGrid")
	FElementalReactionResult Reaction;
};
