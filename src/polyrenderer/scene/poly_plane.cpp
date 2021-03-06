/*
**  Handling drawing a plane (ceiling, floor)
**  Copyright (c) 2016 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include <stdlib.h>
#include "templates.h"
#include "doomdef.h"
#include "sbar.h"
#include "r_data/r_translate.h"
#include "poly_plane.h"
#include "poly_portal.h"
#include "polyrenderer/poly_renderer.h"
#include "r_sky.h"
#include "polyrenderer/scene/poly_light.h"
#include "p_lnspec.h"

EXTERN_CVAR(Int, r_3dfloors)

void RenderPolyPlane::Render3DPlanes(const TriMatrix &worldToClip, const PolyClipPlane &clipPlane, subsector_t *sub, uint32_t stencilValue)
{
	if (!r_3dfloors || sub->sector->CenterFloor() == sub->sector->CenterCeiling())
		return;

	const auto &viewpoint = PolyRenderer::Instance()->Viewpoint;

	auto frontsector = sub->sector;
	auto &ffloors = frontsector->e->XFloor.ffloors;

	// 3D floor floors
	for (int i = 0; i < (int)ffloors.Size(); i++)
	{
		F3DFloor *fakeFloor = ffloors[i];
		if (!(fakeFloor->flags & FF_EXISTS)) continue;
		if (!fakeFloor->model) continue;
		//if (!(fakeFloor->flags & FF_NOSHADE) || (fakeFloor->flags & (FF_RENDERPLANES | FF_RENDERSIDES)))
		//	R_3D_AddHeight(fakeFloor->top.plane, frontsector);
		if (!(fakeFloor->flags & FF_RENDERPLANES)) continue;
		if (fakeFloor->alpha == 0) continue;
		if (fakeFloor->flags & FF_THISINSIDE && fakeFloor->flags & FF_INVERTSECTOR) continue;
		//fakeFloor->alpha

		double fakeHeight = fakeFloor->top.plane->ZatPoint(frontsector->centerspot);
		if (fakeFloor->bottom.plane->isSlope() || (fakeHeight < viewpoint.Pos.Z && fakeHeight > frontsector->floorplane.ZatPoint(frontsector->centerspot)))
		{
			RenderPolyPlane plane;
			plane.Render3DFloor(worldToClip, clipPlane, sub, stencilValue, false, fakeFloor);
		}
	}

	// 3D floor ceilings
	for (int i = 0; i < (int)ffloors.Size(); i++)
	{
		F3DFloor *fakeFloor = ffloors[i];
		if (!(fakeFloor->flags & FF_EXISTS)) continue;
		if (!fakeFloor->model) continue;
		//if (!(fakeFloor->flags & FF_NOSHADE) || (fakeFloor->flags & (FF_RENDERPLANES | FF_RENDERSIDES)))
		//	R_3D_AddHeight(fakeFloor->bottom.plane, frontsector);
		if (!(fakeFloor->flags & FF_RENDERPLANES)) continue;
		if (fakeFloor->alpha == 0) continue;
		if (!(fakeFloor->flags & FF_THISINSIDE) && (fakeFloor->flags & (FF_SWIMMABLE | FF_INVERTSECTOR)) == (FF_SWIMMABLE | FF_INVERTSECTOR)) continue;
		//fakeFloor->alpha

		double fakeHeight = fakeFloor->bottom.plane->ZatPoint(frontsector->centerspot);
		if (fakeFloor->bottom.plane->isSlope() || (fakeHeight > viewpoint.Pos.Z && fakeHeight < frontsector->ceilingplane.ZatPoint(frontsector->centerspot)))
		{
			RenderPolyPlane plane;
			plane.Render3DFloor(worldToClip, clipPlane, sub, stencilValue, true, fakeFloor);
		}
	}
}

void RenderPolyPlane::RenderPlanes(const TriMatrix &worldToClip, const PolyClipPlane &clipPlane, PolyCull &cull, subsector_t *sub, uint32_t stencilValue, double skyCeilingHeight, double skyFloorHeight, std::vector<std::unique_ptr<PolyDrawSectorPortal>> &sectorPortals)
{
	if (sub->sector->CenterFloor() == sub->sector->CenterCeiling())
		return;

	RenderPolyPlane plane;
	plane.Render(worldToClip, clipPlane, cull, sub, stencilValue, true, skyCeilingHeight, sectorPortals);
	plane.Render(worldToClip, clipPlane, cull, sub, stencilValue, false, skyFloorHeight, sectorPortals);
}

void RenderPolyPlane::Render3DFloor(const TriMatrix &worldToClip, const PolyClipPlane &clipPlane, subsector_t *sub, uint32_t stencilValue, bool ceiling, F3DFloor *fakeFloor)
{
	FTextureID picnum = ceiling ? *fakeFloor->bottom.texture : *fakeFloor->top.texture;
	FTexture *tex = TexMan(picnum);
	if (tex->UseType == FTexture::TEX_Null)
		return;

	PolyCameraLight *cameraLight = PolyCameraLight::Instance();

	int lightlevel = 255;
	bool foggy = false;
	if (cameraLight->FixedLightLevel() < 0 && sub->sector->e->XFloor.lightlist.Size())
	{
		lightlist_t *light = P_GetPlaneLight(sub->sector, &sub->sector->ceilingplane, false);
		//basecolormap = light->extra_colormap;
		lightlevel = *light->p_lightlevel;
	}

	int actualextralight = foggy ? 0 : PolyRenderer::Instance()->Viewpoint.extralight << 4;
	lightlevel = clamp(lightlevel + actualextralight, 0, 255);

	UVTransform xform(ceiling ? fakeFloor->top.model->planes[sector_t::ceiling].xform : fakeFloor->top.model->planes[sector_t::floor].xform, tex);

	TriVertex *vertices = PolyRenderer::Instance()->FrameMemory.AllocMemory<TriVertex>(sub->numlines);
	if (ceiling)
	{
		for (uint32_t i = 0; i < sub->numlines; i++)
		{
			seg_t *line = &sub->firstline[i];
			vertices[sub->numlines - 1 - i] = PlaneVertex(line->v1, fakeFloor->bottom.plane->ZatPoint(line->v1), xform);
		}
	}
	else
	{
		for (uint32_t i = 0; i < sub->numlines; i++)
		{
			seg_t *line = &sub->firstline[i];
			vertices[i] = PlaneVertex(line->v1, fakeFloor->top.plane->ZatPoint(line->v1), xform);
		}
	}

	PolyDrawArgs args;
	args.SetLight(GetColorTable(sub->sector->Colormap), lightlevel, PolyRenderer::Instance()->Light.WallGlobVis(foggy), false);
	args.SetTransform(&worldToClip);
	args.SetStyle(TriBlendMode::TextureOpaque);
	args.SetFaceCullCCW(true);
	args.SetStencilTestValue(stencilValue);
	args.SetWriteStencil(true, stencilValue + 1);
	args.SetTexture(tex);
	args.SetClipPlane(clipPlane);
	args.DrawArray(vertices, sub->numlines, PolyDrawMode::TriangleFan);
}

void RenderPolyPlane::Render(const TriMatrix &worldToClip, const PolyClipPlane &clipPlane, PolyCull &cull, subsector_t *sub, uint32_t stencilValue, bool ceiling, double skyHeight, std::vector<std::unique_ptr<PolyDrawSectorPortal>> &sectorPortals)
{
	const auto &viewpoint = PolyRenderer::Instance()->Viewpoint;
	bool foggy = false;
	
	sector_t *fakesector = sub->sector->heightsec;
	if (fakesector && (fakesector == sub->sector || (fakesector->MoreFlags & SECF_IGNOREHEIGHTSEC) == SECF_IGNOREHEIGHTSEC))
		fakesector = nullptr;

	bool fakeflooronly = fakesector && (fakesector->MoreFlags & SECF_FAKEFLOORONLY) == SECF_FAKEFLOORONLY;

	FTextureID picnum;
	bool ccw;
	sector_t *frontsector;
	if (fakesector)
	{
		// Floor and ceiling texture needs to be swapped sometimes? Why?? :(

		if (viewpoint.Pos.Z < fakesector->floorplane.Zat0()) // In water
		{
			if (ceiling)
			{
				picnum = fakesector->GetTexture(sector_t::ceiling);
				ceiling = false;
				frontsector = fakesector;
				ccw = false;
			}
			else
			{
				picnum = fakesector->GetTexture(sector_t::floor);
				frontsector = sub->sector;
				ccw = true;
			}
		}
		else if (viewpoint.Pos.Z >= fakesector->ceilingplane.Zat0() && !fakeflooronly) // In ceiling water
		{
			if (ceiling)
			{
				picnum = fakesector->GetTexture(sector_t::ceiling);
				frontsector = sub->sector;
				ccw = true;
			}
			else
			{
				picnum = fakesector->GetTexture(sector_t::floor);
				frontsector = fakesector;
				ccw = false;
				ceiling = true;
			}
		}
		else if (!ceiling) // Water surface
		{
			picnum = fakesector->GetTexture(sector_t::ceiling);
			frontsector = fakesector;
			ccw = true;
		}
		else if (!fakeflooronly) // Ceiling water surface
		{
			picnum = fakesector->GetTexture(sector_t::floor);
			frontsector = fakesector;
			ccw = true;
		}
		else // Upper ceiling
		{
			picnum = sub->sector->GetTexture(sector_t::ceiling);
			ccw = true;
			frontsector = sub->sector;
		}
	}
	else
	{
		picnum = sub->sector->GetTexture(ceiling ? sector_t::ceiling : sector_t::floor);
		ccw = true;
		frontsector = sub->sector;
	}

	FTexture *tex = TexMan(picnum);
	if (tex->UseType == FTexture::TEX_Null)
		return;

	std::vector<PolyPortalSegment> portalSegments;
	FSectorPortal *portal = sub->sector->ValidatePortal(ceiling ? sector_t::ceiling : sector_t::floor);
	PolyDrawSectorPortal *polyportal = nullptr;
	if (portal && (portal->mFlags & PORTSF_INSKYBOX) == PORTSF_INSKYBOX) // Do not recurse into portals we already recursed into
		portal = nullptr;

	bool isSky = portal || picnum == skyflatnum;

	if (portal)
	{
		// Skip portals not facing the camera
		if ((ceiling && frontsector->ceilingplane.PointOnSide(viewpoint.Pos) < 0) ||
			(!ceiling && frontsector->floorplane.PointOnSide(viewpoint.Pos) < 0))
		{
			return;
		}

		for (auto &p : sectorPortals)
		{
			if (p->Portal == portal) // To do: what other criteria do we need to check for?
			{
				polyportal = p.get();
				break;
			}
		}
		if (!polyportal)
		{
			sectorPortals.push_back(std::unique_ptr<PolyDrawSectorPortal>(new PolyDrawSectorPortal(portal, ceiling)));
			polyportal = sectorPortals.back().get();
		}

#if 0
		// Calculate portal clipping
		portalSegments.reserve(sub->numlines);
		for (uint32_t i = 0; i < sub->numlines; i++)
		{
			seg_t *line = &sub->firstline[i];

			DVector2 pt1 = line->v1->fPos() - viewpoint.Pos;
			DVector2 pt2 = line->v2->fPos() - viewpoint.Pos;
			bool backside = pt1.Y * (pt1.X - pt2.X) + pt1.X * (pt2.Y - pt1.Y) >= 0;
			if (!backside)
			{
				angle_t angle1, angle2;
				if (cull.GetAnglesForLine(line->v1->fX(), line->v1->fY(), line->v2->fX(), line->v2->fY(), angle1, angle2))
					portalSegments.push_back({ angle1, angle2 });
			}
			else
			{
				angle_t angle1, angle2;
				if (cull.GetAnglesForLine(line->v2->fX(), line->v2->fY(), line->v1->fX(), line->v1->fY(), angle1, angle2))
					portalSegments.push_back({ angle1, angle2 });
			}
		}
#endif
	}

	UVTransform transform(ceiling ? frontsector->planes[sector_t::ceiling].xform : frontsector->planes[sector_t::floor].xform, tex);

	TriVertex *vertices = PolyRenderer::Instance()->FrameMemory.AllocMemory<TriVertex>(sub->numlines);

	if (ceiling)
	{
		for (uint32_t i = 0; i < sub->numlines; i++)
		{
			seg_t *line = &sub->firstline[i];
			vertices[sub->numlines - 1 - i] = PlaneVertex(line->v1, isSky ? skyHeight : frontsector->ceilingplane.ZatPoint(line->v1), transform);
		}
	}
	else
	{
		for (uint32_t i = 0; i < sub->numlines; i++)
		{
			seg_t *line = &sub->firstline[i];
			vertices[i] = PlaneVertex(line->v1, isSky ? skyHeight : frontsector->floorplane.ZatPoint(line->v1), transform);
		}
	}

	int lightlevel = frontsector->lightlevel;
	int actualextralight = foggy ? 0 : PolyRenderer::Instance()->Viewpoint.extralight << 4;
	lightlevel = clamp(lightlevel + actualextralight, 0, 255);

	PolyDrawArgs args;
	args.SetLight(GetColorTable(frontsector->Colormap, frontsector->SpecialColors[ceiling]), lightlevel, PolyRenderer::Instance()->Light.WallGlobVis(foggy), false);
	//args.SetSubsectorDepth(isSky ? RenderPolyScene::SkySubsectorDepth : subsectorDepth);
	args.SetTransform(&worldToClip);
	args.SetFaceCullCCW(ccw);
	args.SetStencilTestValue(stencilValue);
	args.SetWriteStencil(true, stencilValue + 1);
	args.SetClipPlane(clipPlane);

	if (!isSky)
	{
		args.SetTexture(tex);
		args.SetStyle(TriBlendMode::TextureOpaque);
		args.DrawArray(vertices, sub->numlines, PolyDrawMode::TriangleFan);
	}
	else
	{
		if (portal)
		{
			args.SetWriteStencil(true, polyportal->StencilValue);
			polyportal->Shape.push_back({ vertices, (int)sub->numlines, ccw });
		}
		else
		{
			args.SetWriteStencil(true, 255);
		}

		args.SetWriteColor(false);
		args.SetWriteDepth(false);
		args.DrawArray(vertices, sub->numlines, PolyDrawMode::TriangleFan);

		for (uint32_t i = 0; i < sub->numlines; i++)
		{
			TriVertex *wallvert = PolyRenderer::Instance()->FrameMemory.AllocMemory<TriVertex>(4);

			seg_t *line = &sub->firstline[i];

			double skyBottomz1 = frontsector->ceilingplane.ZatPoint(line->v1);
			double skyBottomz2 = frontsector->ceilingplane.ZatPoint(line->v2);
			if (line->backsector)
			{
				sector_t *backsector = (line->backsector != line->frontsector) ? line->backsector : line->frontsector;

				double frontceilz1 = frontsector->ceilingplane.ZatPoint(line->v1);
				double frontfloorz1 = frontsector->floorplane.ZatPoint(line->v1);
				double frontceilz2 = frontsector->ceilingplane.ZatPoint(line->v2);
				double frontfloorz2 = frontsector->floorplane.ZatPoint(line->v2);

				double backceilz1 = backsector->ceilingplane.ZatPoint(line->v1);
				double backfloorz1 = backsector->floorplane.ZatPoint(line->v1);
				double backceilz2 = backsector->ceilingplane.ZatPoint(line->v2);
				double backfloorz2 = backsector->floorplane.ZatPoint(line->v2);

				double topceilz1 = frontceilz1;
				double topceilz2 = frontceilz2;
				double topfloorz1 = MIN(backceilz1, frontceilz1);
				double topfloorz2 = MIN(backceilz2, frontceilz2);
				double bottomceilz1 = MAX(frontfloorz1, backfloorz1);
				double bottomceilz2 = MAX(frontfloorz2, backfloorz2);
				double middleceilz1 = topfloorz1;
				double middleceilz2 = topfloorz2;
				double middlefloorz1 = MIN(bottomceilz1, middleceilz1);
				double middlefloorz2 = MIN(bottomceilz2, middleceilz2);

				bool bothSkyCeiling = frontsector->GetTexture(sector_t::ceiling) == skyflatnum && backsector->GetTexture(sector_t::ceiling) == skyflatnum;

				bool closedSector = backceilz1 == backfloorz1 && backceilz2 == backfloorz2;
				if (ceiling && bothSkyCeiling && closedSector)
				{
					skyBottomz1 = middlefloorz1;
					skyBottomz2 = middlefloorz2;
				}
				else if (bothSkyCeiling)
				{
					continue;
				}
			}
			else if (portal && line->linedef && line->linedef->special == Line_Horizon)
			{
				// Not entirely correct as this closes the line horizon rather than allowing the floor to continue to infinity
				skyBottomz1 = frontsector->floorplane.ZatPoint(line->v1);
				skyBottomz2 = frontsector->floorplane.ZatPoint(line->v2);
			}

			if (ceiling)
			{
				wallvert[0] = PlaneVertex(line->v1, skyHeight, transform);
				wallvert[1] = PlaneVertex(line->v2, skyHeight, transform);
				wallvert[2] = PlaneVertex(line->v2, skyBottomz2, transform);
				wallvert[3] = PlaneVertex(line->v1, skyBottomz1, transform);
			}
			else
			{
				wallvert[0] = PlaneVertex(line->v1, frontsector->floorplane.ZatPoint(line->v1), transform);
				wallvert[1] = PlaneVertex(line->v2, frontsector->floorplane.ZatPoint(line->v2), transform);
				wallvert[2] = PlaneVertex(line->v2, skyHeight, transform);
				wallvert[3] = PlaneVertex(line->v1, skyHeight, transform);
			}

			args.DrawArray(wallvert, 4, PolyDrawMode::TriangleFan);
			
			if (portal)
			{
				polyportal->Shape.push_back({ wallvert, 4, ccw });
			}
		}
	}
}

TriVertex RenderPolyPlane::PlaneVertex(vertex_t *v1, double height, const UVTransform &transform)
{
	TriVertex v;
	v.x = (float)v1->fPos().X;
	v.y = (float)v1->fPos().Y;
	v.z = (float)height;
	v.w = 1.0f;
	v.u = transform.GetU(v.x, v.y);
	v.v = transform.GetV(v.x, v.y);
	return v;
}

RenderPolyPlane::UVTransform::UVTransform(const FTransform &transform, FTexture *tex)
{
	if (tex)
	{
		xscale = (float)(transform.xScale * tex->Scale.X / tex->GetWidth());
		yscale = (float)(transform.yScale * tex->Scale.Y / tex->GetHeight());

		double planeang = (transform.Angle + transform.baseAngle).Radians();
		cosine = (float)cos(planeang);
		sine = (float)sin(planeang);

		xOffs = (float)transform.xOffs;
		yOffs = (float)transform.yOffs;
	}
	else
	{
		xscale = 1.0f / 64.0f;
		yscale = 1.0f / 64.0f;
		cosine = 1.0f;
		sine = 0.0f;
		xOffs = 0.0f;
		yOffs = 0.0f;
	}
}

float RenderPolyPlane::UVTransform::GetU(float x, float y) const
{
	return (xOffs + x * cosine - y * sine) * xscale;
}

float RenderPolyPlane::UVTransform::GetV(float x, float y) const
{
	return (yOffs - x * sine - y * cosine) * yscale;
}
