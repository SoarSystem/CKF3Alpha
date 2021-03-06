#include <metahook.h>
#include "exportfuncs.h"
#include "engfuncs.h"
#include "client.h"
#include "qgl.h"
#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"
#include "util.h"
#include "tent.h"
#include "weapon.h"
#include "cvar.h"
#include <pm_defs.h>
#include <pm_shared.h>

const char g_szGameName[] = "Chicken Fortress 3";

extern int g_mouse_state;
extern int g_mouse_oldstate;
extern bool g_bGameUIActivate;
extern cl_entity_t g_entTeamMenu[4];
extern cl_entity_t g_entClassMenu[2];

void HudBase_MouseUp(int mx, int my);
void HudBase_MouseDown(int mx, int my);
bool HudBase_IsFullScreenMenu(void);

void StudioSetupModel(int bodypart, void **ppbodypart, void **ppsubmodel);
void StudioModelRenderer_InstallHook(void);
void CL_TraceEntity(void);

void T_VidInit(void);
void T_UpdateTEnts(void);
void T_DrawTEnts(void);

void Cvar_HudInit(void);

void R_UpdateParticles(void);
void R_DrawParticles(void);
void R_Particles_VidInit(void);

void UpdateBuildables(void);

void UserMsg_InstallHook(void);

cl_exportfuncs_t gClientfuncs =
{
	Initialize,
	HUD_Init,
	HUD_VidInit,
	HUD_Redraw,
	HUD_UpdateClientData,
	NULL,
	HUD_PlayerMove,
	HUD_PlayerMoveInit,
	HUD_PlayerMoveTexture,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	CL_CreateMove,
	NULL,
	NULL,
	NULL,
	NULL,
	V_CalcRefdef,
	HUD_AddEntity,
	NULL,
	HUD_DrawNormalTriangles,
	HUD_DrawTransparentTriangles,
	HUD_StudioEvent,
	HUD_PostRunCmd,
	NULL,
	NULL,
	HUD_ProcessPlayerState,
	NULL,
	NULL,
	NULL,
	NULL,
	HUD_Frame,
	HUD_Key_Event,
	HUD_TempEntUpdate,
	NULL,
	NULL,
	NULL,
	HUD_GetStudioModelInterface,
	NULL,
	NULL,
	NULL
};

ckf_vars_t gCKFVars = 
{
	&g_iTeam,
	&g_iClass,
	&g_iDesiredClass,
	&g_iHealth,
	&g_iMaxHealth,
	&g_iRoundStatus,
	&g_iLimitTeams,
	&g_iMaxRoundTime,
	&g_flRoundEndTime,
	&g_pTraceEntity,
	(CKFPlayerInfo *)g_PlayerInfo,
	(CKFPlayerStats *)&g_PlayerStats,
	(CKFClientPlayer *)&g_Player
};

cl_enginefunc_t gEngfuncs;
engine_studio_api_t *gpEngineStudio;
engine_studio_api_t IEngineStudio;
ref_params_t refparams;
cl_entity_t **CurrentEntity;
studiohdr_t **StudioHeader;

float *ev_punchangle;

void *gpViewPortInterface;

int *cls_viewmodel_sequence;
int *cls_viewmodel_body;
float *cls_viewmodel_starttime;

int g_bRenderPlayerWeapon;
int g_fLOD;
int g_iLODLevel;

int g_iViewModelBody;
int g_iViewModelSkin;

double g_flClientTime;
double g_flFrameTime;
int g_iHookSetupBones;
float g_flTraceDistance;
cl_entity_t *g_pTraceEntity;
qboolean g_iHudVidInitalized;
int g_RefSupportExt;
cl_entity_t *cl_viewent;

int Initialize(struct cl_enginefuncs_s *pEnginefuncs, int iVersion)
{
	memcpy(&gEngfuncs, pEnginefuncs, sizeof(gEngfuncs));

#define CL_VIEWMODEL_SEQUENCE_SIG "\xA3\x2A\x2A\x2A\x2A\xC7\x05"
	DWORD addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)g_pMetaSave->pEngineFuncs->pfnWeaponAnim, 0x50, CL_VIEWMODEL_SEQUENCE_SIG, sizeof(CL_VIEWMODEL_SEQUENCE_SIG)-1);
	if(!addr)
		SIG_NOT_FOUND("cl.viewmodel_sequence");

	cls_viewmodel_sequence = *(int **)(addr + 1);
	cls_viewmodel_starttime = *(float **)(addr + 7);
	cls_viewmodel_body = *(int **)(addr + 17);

	QGL_Init();

	refdef = gRefExports.R_GetRefDef();

	return 1;
}

void HUD_Init(void)
{
	HudBase_Init();
	Cvar_Init();
	Cvar_HudInit();
	EV_HookEvents();
	UserMsg_InstallHook();
	
	//this is register after HUD_Init
	gHUD_m_pip = gEngfuncs.pfnGetCvarPointer("spec_pip");
}

int HUD_VidInit(void)
{
	T_VidInit();
	HudBase_VidInit();
	R_Particles_VidInit();

	g_iHudVidInitalized = true;

	cl_viewent = gEngfuncs.GetViewModel();
	//bug fix: this will crash client when changing level if we don't set it null
	g_pTraceEntity = NULL;

	gEngfuncs.pfnClientCmd("bind \",\" \"chooseclass\"");
	gEngfuncs.pfnClientCmd("bind \".\" \"chooseteam\"");

	return 1;
}

int HUD_Redraw(float time, int intermission)
{
	HudBase_Redraw(time, intermission);

	return 1;
}

int HUD_Key_Event( int eventcode, int keynum, const char *pszCurrentBinding )
{
	if(HudBase_KeyEvent(eventcode, keynum, pszCurrentBinding))
		return 0;

	return 1;
}

r_studio_interface_t studio_interface =
{
	STUDIO_INTERFACE_VERSION,
	R_StudioDrawModel,
	R_StudioDrawPlayer,
};

model_t *Mod_LoadModel(model_t *mod, qboolean crash, qboolean trackCRC)
{
	int needload = mod->needload;

	model_t *result = gHookFuncs.Mod_LoadModel(mod, crash, trackCRC);

	if(result)
	{
		if(result->type == mod_studio && needload == NL_CLIENT && result->needload != NL_CLIENT)
		{
			result->needload = needload;
		}
	}
	return result;
}

int HUD_GetStudioModelInterface( int version, struct r_studio_interface_s **ppinterface, struct engine_studio_api_s *pstudio )
{
	memcpy(&IEngineStudio, pstudio, sizeof(engine_studio_api_t));

	DWORD addr;

	addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)pstudio->GetCurrentEntity, 0x10, "\xA1", 1);
	if(!addr)
		SIG_NOT_FOUND("currententity");
	CurrentEntity = *(cl_entity_t ***)(addr + 0x1);

	addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)pstudio->StudioSetHeader, 0x10, "\xA3", 1);
	if(!addr)
		SIG_NOT_FOUND("pstudiohdr");
	StudioHeader = *(studiohdr_t ***)(addr + 0x1);

	pstudio->StudioSetupModel = StudioSetupModel;

	gpEngineStudio = pstudio;
	
	*ppinterface = &studio_interface;

	R_StudioInit();

	//Fatal bug fix: CL_PrecacheResourses will overwrite model's needload flag to 0 even if it's never unloaded
#define MOD_LOADMODEL_SIG "\x6A\x01\x57\xE8"
	addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)pstudio->Mod_Extradata, 0x100, MOD_LOADMODEL_SIG, sizeof(MOD_LOADMODEL_SIG)-1);
	if(!addr)
		SIG_NOT_FOUND("Mod_LoadModel");
	gHookFuncs.Mod_LoadModel = (model_t *(*)(model_t *, qboolean, qboolean))GetCallAddress(addr+3);

	g_pMetaHookAPI->InlineHook((void *)gHookFuncs.Mod_LoadModel, Mod_LoadModel,(void *&)gHookFuncs.Mod_LoadModel);

	return 1;
}

void V_CalcRefdef(struct ref_params_s *pparams)
{
	//if(HudBase_IsFullScreenMenu() && !g_bGameUIActivate)
	//{
	//	pparams->viewangles[0] = 0;
	//	pparams->viewangles[1] = 0;
	//	pparams->viewangles[2] = 0;
	//	pparams->vieworg[0] = 0;
	//	pparams->vieworg[1] = 0;
	//	pparams->vieworg[2] = 0;
	//}
	memcpy(&refparams, pparams, sizeof(ref_params_t));
	CL_TraceEntity();
}

void HUD_TempEntUpdate(double frametime, double client_time, double cl_gravity, struct tempent_s **ppTempEntFree, struct tempent_s **ppTempEntActive, 	int (*pfnAddVisibleEntity)(cl_entity_t *),	void (*pfnTempEntPlaySound)( TEMPENTITY *, float damp))
{
	g_flClientTime = client_time;
	g_flFrameTime = frametime;

	UpdateBuildables();
	T_UpdateTEnts();
}

void HUD_DrawNormalTriangles(void)
{
	T_DrawTEnts();
}

void HUD_DrawTransparentTriangles(void)
{
	if(gRefExports.R_GetDrawPass() == r_draw_normal)
		R_UpdateParticles();
	R_DrawParticles();
}

void R_UpdateViewModel(void)
{
	*CurrentEntity = cl_viewent;

	cl_entity_t *pViewEntity = gEngfuncs.GetEntityByIndex(refparams.viewentity);

	if(!refparams.viewentity || !pViewEntity)
		return;

	cl_viewent->curstate.body = CL_GetViewBody();
	cl_viewent->curstate.rendermode = kRenderNormal;
	cl_viewent->curstate.renderfx = kRenderFxNone;

	cl_viewent->curstate.team = 0;

	if(pViewEntity->player)
	{
		cl_viewent->curstate.skin = pViewEntity->curstate.skin;
		cl_viewent->curstate.team = pViewEntity->curstate.team;

		if(pViewEntity->curstate.effects & EF_CRITBOOST)
			cl_viewent->curstate.effects |= EF_CRITBOOST;
		else
			cl_viewent->curstate.effects &= ~EF_CRITBOOST;

		if(pViewEntity->curstate.effects & EF_INVULNERABLE)
			cl_viewent->curstate.effects |= EF_INVULNERABLE;
		else
			cl_viewent->curstate.effects &= ~EF_INVULNERABLE;
	}

	if(g_SpyWatch.show)
	{
		cl_viewent->curstate.renderfx = kRenderFxCloak;
		cl_viewent->curstate.renderamt = g_SpyWatch.ent.curstate.renderamt;

		g_SpyWatch.ent.curstate.effects = cl_viewent->curstate.effects;
	}
	else if(pViewEntity->curstate.renderfx == kRenderFxCloak)
	{
		cl_viewent->curstate.renderfx = kRenderFxCloak;
		cl_viewent->curstate.renderamt = pViewEntity->curstate.renderamt;
	}
}

int HUD_AddEntity(int iType, struct cl_entity_s *pEntity, const char *szModel)
{
	if(strstr(szModel, "sniperdot"))
	{
		if(pEntity->curstate.owner == gEngfuncs.GetLocalPlayer()->index)
		{
			pEntity->curstate.renderamt = 0;
			pEntity->curstate.effects |= EF_NODRAW;
			return 0;
		}
	}

	return 1;
}

void StudioSetupModel(int bodypart, void **ppbodypart, void **ppsubmodel)
{
	if(!g_fLOD)
	{
		IEngineStudio.StudioSetupModel(bodypart, ppbodypart, ppsubmodel);
		return;
	}
	cl_entity_t *pEntity = (*CurrentEntity);
	int iSaveBody = (*CurrentEntity)->curstate.body;

	bodypart = bodypart % (*StudioHeader)->numbodyparts;
	mstudiobodyparts_t *pbodypart = (mstudiobodyparts_t *)((byte *)(*StudioHeader) + (*StudioHeader)->bodypartindex) + bodypart;
	if(pEntity->player)//player model
	{
		if(g_bRenderPlayerWeapon)//player weapon body
		{
			(*CurrentEntity)->curstate.body = (*CurrentEntity)->curstate.scale + g_iLODLevel;
		}
		else if(g_iLODLevel > 0 && bodypart == 1)
		{
			(*CurrentEntity)->curstate.body += g_iLODLevel * pbodypart->base;
		}
	}
	else if(g_iLODLevel > 0)
	{
		(*CurrentEntity)->curstate.body += g_iLODLevel * pbodypart->base;
	}
	IEngineStudio.StudioSetupModel(bodypart, ppbodypart, ppsubmodel);
	(*CurrentEntity)->curstate.body = iSaveBody;
}

int HUD_UpdateClientData(client_data_t *cldata, float flTime)
{
	if(g_iForceFOV > 0)
	{
		cldata->fov = g_iForceFOV;
	}
	return 1;
}

void HudWeaponAnim(int iSequence)
{
	*cls_viewmodel_sequence = iSequence;
	*cls_viewmodel_starttime = 0;
}

void HudWeaponAnimEx(int iSequence, int iBody, int iSkin, float flAnimtime)
{
	if(iSequence != -1)
	{
		*cls_viewmodel_sequence = iSequence;
		*cls_viewmodel_starttime = flAnimtime;
	}
	if(iBody != -1)
		g_iViewModelBody = iBody;
	if(iSkin != -1)
		g_iViewModelSkin = iSkin;
}

void HUD_PlayerMove(struct playermove_s *ppmove, int server)
{
	PM_Move(ppmove, server);
}

void HUD_PlayerMoveInit(struct playermove_s *ppmove)
{
	cl_pmove = ppmove;
	PM_Init(ppmove);
}

char HUD_PlayerMoveTexture(char *name)
{
	return PM_FindTextureType(name);
}

void Hook_SV_StudioSetupBones(model_t *pModel, float frame, int sequence, vec_t *angles, vec_t *origin, const byte *pcontroller, const byte *pblending, int iBone, const edict_t *edict)
{
	if(g_iHookSetupBones && strstr(pModel->name, "player"))
	{
		g_StudioRenderer.PM_StudioSetupBones(sequence);//hack hack
		return;
	}
	gHookFuncs.SV_StudioSetupBones(pModel, frame, sequence, angles, origin, pcontroller, pblending, iBone, edict);
}

void HUD_ProcessPlayerState( struct entity_state_s *dst, const struct entity_state_s *src )
{
	cl_entity_t *player = gEngfuncs.GetLocalPlayer();
	if ( dst->number == player->index )
	{
		g_iUser1 = src->iuser1;
		g_iUser2 = src->iuser2;
		g_iUser3 = src->iuser3;
	}
}

void CL_CreateMove ( float frametime, struct usercmd_s *cmd, int active )
{
	if(g_WeaponSelect)
	{
		cmd->weaponselect = g_WeaponSelect;
		g_WeaponSelect = 0;
	}
}

void R_ShotgunMuzzle(cl_entity_t *pEntity, int attachment);

void HUD_StudioEvent( const struct mstudioevent_s *ev, const struct cl_entity_s *pEntity )
{
	switch( ev->event )
	{
	case 5001:
		//R_ShotgunMuzzle( (cl_entity_t *)pEntity, 0 );
		break;
	case 5011:
		//R_ShotgunMuzzle( (cl_entity_t *)pEntity, 1 );
		break;
	case 5021:
		//R_ShotgunMuzzle( (cl_entity_t *)pEntity, 2 );
		break;
	case 5031:
		//R_ShotgunMuzzle( (cl_entity_t *)pEntity, 3 );
		break;
	case 5002:
		gEngfuncs.pEfxAPI->R_SparkEffect( (float *)&pEntity->attachment[0], atoi( ev->options), -100, 100 );
		break;
	// Client side sound
	case 5004:		
		gEngfuncs.pfnPlaySoundByNameAtLocation( (char *)ev->options, 1.0, (float *)&pEntity->attachment[0] );
		break;
	default:
		break;
	}
}

void HUD_Frame(double time)
{
	g_StudioRenderer.ResetEntityBones();
}