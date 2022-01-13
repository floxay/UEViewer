#include "Core.h"
#include "UnCore.h"

#include "UnObject.h"
#include "UnrealMaterial/UnMaterial.h"

#include "Mesh/StaticMesh.h"

#include "UnrealMesh/UnMathTools.h"

#include "Exporters.h"
#define MIRROR_MESH		1
#define FLIP_UVS		1

static void ExportStaticMeshLod(const CStaticMeshLod& Lod, FArchive& Ar)
{
	guard(ExportStaticMeshLod);

	uint8 magic[4]{ 'X', 'A', 'Y', 0x02 };
	uint8 version = 0x1;
	uint8 reserved[3]{};

	uint32 vertexCount = Lod.NumVerts;
	uint32 faceCount = 0;
	uint16 sectionCount = Lod.Sections.Num();
	TArray<uint32> nextSectionFaceStartIndices;
	TArray<FString> materialNames;
	nextSectionFaceStartIndices.Empty(sectionCount);
	materialNames.Empty(sectionCount);
	for (int i = 0; i < sectionCount; i++)
	{
		CMeshSection section = Lod.Sections[i];
		faceCount += section.NumFaces;
		nextSectionFaceStartIndices.Add(faceCount);
		char dummyName[24];	// max of 100000000 dummy materials in a single mesh, should be enough lol
		appSprintf(ARRAY_ARG(dummyName), "dummy_material_%d", i);
		materialNames.Add((FString)(section.Material ? section.Material->Name : dummyName));
	}
	uint8 texCoordCount = Lod.NumTexCoords;
	uint8 hasVertexColors = (bool)Lod.VertexColors;

	Ar.Serialize(ARRAY_ARG(magic));
	Ar << version;
	Ar.Serialize(ARRAY_ARG(reserved));
	Ar << vertexCount << faceCount;
	Ar << texCoordCount << hasVertexColors << sectionCount;
	for (int i = 0; i < sectionCount; i++)
	{
		Ar << materialNames[i];
		Ar << nextSectionFaceStartIndices[i];
	}

	// vert position, normal and UV0
	for (int i = 0; i < vertexCount; i++)
	{
		CVec3 vertPos = Lod.Verts[i].Position;
		CVec3 normal;
		CMeshUVFloat UV = Lod.Verts[i].UV;
		vertPos.Scale(0.01f);
		Unpack(normal, Lod.Verts[i].Normal);
		normal.Normalize();
#if FLIP_UVS
		UV.V = 1 - UV.V;
#endif
#if MIRROR_MESH
		normal.Y = -normal.Y;
		vertPos.Y = -vertPos.Y;
#endif
		Ar << vertPos.X << normal.X;
		Ar << vertPos.Y << normal.Y;
		Ar << vertPos.Z << normal.Z;
		Ar << UV.U << UV.V;
	}

	// faces
	CIndexBuffer::IndexAccessor_t Index = Lod.Indices.GetAccessor();
	if (vertexCount <= 0xFFFF + 1)
	{
		for (auto& section : Lod.Sections)
		{
			uint32 sectionFirstIndex = section.FirstIndex;
			for (int i = 0; i < section.NumFaces; i++)
			{
				uint32 firstFaceIdx = i * 3 + sectionFirstIndex;
				uint16 f1 = Index(firstFaceIdx);
				uint16 f2 = Index(firstFaceIdx + 1);
				uint16 f3 = Index(firstFaceIdx + 2);
				Ar << f1 << f2 << f3;
			}
		}
	}
	else
	{
		for (auto& section : Lod.Sections)
		{
			uint32 sectionFirstIndex = section.FirstIndex;
			for (int i = 0; i < section.NumFaces; i++)
			{
				uint32 firstFaceIdx = i * 3 + sectionFirstIndex;
				uint32 f1 = Index(firstFaceIdx);
				uint32 f2 = Index(firstFaceIdx + 1);
				uint32 f3 = Index(firstFaceIdx + 2);
				Ar << f1 << f2 << f3;
			}
		}
	}

	// extrauvs
	for (int UVSet = 1; UVSet < Lod.NumTexCoords; UVSet++)
	{
		CMeshUVFloat* UV = Lod.ExtraUV[UVSet - 1];
		for (int i = 0; i < vertexCount; i++)
		{
#if FLIP_UVS
			UV[i].V = 1 - UV[i].V;
#endif
			Ar << UV[i].U << UV[i].V;
		}
	}

	// vertex colors
	if (Lod.VertexColors)
	{
		Ar.Serialize((void*)Lod.VertexColors, sizeof(FColor) * vertexCount);
	}

	unguard;
}

void ExportStaticMeshXAY(const CStaticMesh *Mesh)
{
	guard(ExportStaticMeshXAY);

	const UObject* OriginalMesh = Mesh->OriginalMesh;
	if (!Mesh->Lods.Num())
	{
		appNotify("Mesh %s has 0 lods", OriginalMesh->Name);
		return;
	}

	if (Mesh->Lods[0].Sections.Num() == 0)
	{
		appNotify("Mesh %s LOD %d has no sections\n", OriginalMesh->Name, 0);
		return;
	}

	FArchive* Ar = CreateExportArchive(OriginalMesh, EFileArchiveOptions::Default, "%s.xay", OriginalMesh->Name);
	if (Ar)
	{
		ExportStaticMeshLod(Mesh->Lods[0], *Ar);
		delete Ar;
	}

	if (OriginalMesh->GetTypeinfo()->NumProps)
	{
		FArchive* PropAr = CreateExportArchive(OriginalMesh, EFileArchiveOptions::TextFile, "%s.props.txt", OriginalMesh->Name);
		if (PropAr)
		{
			OriginalMesh->GetTypeinfo()->SaveProps(OriginalMesh, *PropAr);
			delete PropAr;
		}
	}

	unguard;
}