/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_parse.c  -- parse a message received from the server

#include "client.h"
#include "cl_ui.h"
#include "../qcommon/bg_compat.h"

const char *svc_strings[256] = {
	"svc_bad",

	"svc_nop",
	"svc_gamestate",
	"svc_configstring",
	"svc_baseline",	
	"svc_serverCommand",
	"svc_download",
	"svc_snapshot",
	"svc_centerprint",
	"svc_locprint",
	"svc_cgameMessage"
};

msg_t *cl_currentMSG;

void SHOWNET( msg_t *msg, const char *s) {
	if ( cl_shownet->integer >= 2) {
		Com_Printf ("%3i:%s\n", msg->readcount-1, s);
	}
}


/*
=========================================================================

MESSAGE PARSING

=========================================================================
*/

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
void CL_DeltaEntity (msg_t *msg, clSnapshot_t *frame, int newnum, entityState_t *old, 
					 qboolean unchanged) {
	entityState_t	*state;

	// save the parsed entity state into the big circular buffer so
	// it can be used as the source for a later delta
	state = &cl.parseEntities[cl.parseEntitiesNum & (MAX_PARSE_ENTITIES-1)];

	if ( unchanged ) {
		*state = *old;
	} else {
		MSG_ReadDeltaEntity( msg, old, state, newnum, cls.serverFrameTime);
	}

	if ( state->number == (MAX_GENTITIES-1) ) {
		return;		// entity was delta removed
	}
	cl.parseEntitiesNum++;
	frame->numEntities++;
}

/*
==================
CL_ParsePacketEntities

==================
*/
void CL_ParsePacketEntities( msg_t *msg, clSnapshot_t *oldframe, clSnapshot_t *newframe) {
	int			newnum;
	entityState_t	*oldstate;
	int			oldindex, oldnum;

	newframe->parseEntitiesNum = cl.parseEntitiesNum;
	newframe->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if (!oldframe) {
		oldnum = 99999;
	} else {
		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while ( 1 ) {
		// read the entity index number
		newnum = MSG_ReadEntityNum(msg);

		if ( newnum == (MAX_GENTITIES-1) ) {
			break;
		}

		if ( msg->readcount > msg->cursize ) {
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: end of message");
		}

		while ( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
			}
			CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );
			
			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}
		if (oldnum == newnum) {
			// delta from previous state
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  delta: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, oldstate, qfalse );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if ( oldnum > newnum ) {
			// delta from baseline
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  baseline: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, &cl.entityBaselines[newnum], qfalse );
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while ( oldnum != 99999 ) {
		// one or more entities from the old packet are unchanged
		if ( cl_shownet->integer == 3 ) {
			Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
		}
		CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );
		
		oldindex++;

		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}

/*
================
CL_UnpackNonPVSClient

Unpack info about non-visible client.
================
*/
qboolean CL_UnpackNonPVSClient(int* packed, radarUnpacked_t* unpacked) {
	int x, y;
	int yaw;
	qboolean bValid;
	float range;

	unpacked->clientNum = *packed & (MAX_CLIENTS - 1);
	if (unpacked->clientNum == cl.snap.ps.clientNum) {
		return qfalse;
	}

	x = ((*packed >> 6) & 0x7F) - (MAX_CLIENTS - 1);
	y = ((*packed >> 13) & 0x7F) - (MAX_CLIENTS - 1);
	yaw = (*packed >> 20) & 0x1F;
	bValid = (*packed >> 25) & 1;

	if (com_radar_range && com_radar_range->value) {
		range = com_radar_range->value;
	} else {
		range = 0;
	}

	unpacked->x = x * range / (MAX_CLIENTS - 1);
	unpacked->y = y * range / (MAX_CLIENTS - 1);

	if (!bValid)
	{
		unpacked->x = x * range / (MAX_CLIENTS - 1) * 1024.0;
		unpacked->y = y * range / (MAX_CLIENTS - 1) * 1024.0;
	}

	unpacked->yaw = yaw * 11.25f;

	return qtrue;
}

/*
================
CL_ReadNonPVSClient

Reads packed info about non-visible client.
================
*/
void CL_ReadNonPVSClient(int radarInfo) {
	radarUnpacked_t unpacked;

	if (!cge) {
		return;
	}

	if (CL_UnpackNonPVSClient(&radarInfo, &unpacked)) {
		unpacked.x += cl.snap.ps.origin[0];
		unpacked.y += cl.snap.ps.origin[1];
		cge->CG_ReadNonPVSClient(&unpacked);
	}
}

/*
================
CL_ParseSnapshot

If the snapshot is parsed properly, it will be copied to
cl.snap and saved in cl.snapshots[].  If the snapshot is invalid
for any reason, no changes to the state will be made at all.
================
*/
void CL_ParseSnapshot( msg_t *msg ) {
	int			len;
	clSnapshot_t	*old;
	clSnapshot_t	newSnap;
	int			deltaNum;
	int			oldMessageNum;
	int			i, packetNum;

	// get the reliable sequence acknowledge number
	// NOTE: now sent with all server to client messages
	//clc.reliableAcknowledge = MSG_ReadLong( msg );

	// read in the new snapshot to a temporary buffer
	// we will only copy to cl.snap if it is valid
	Com_Memset (&newSnap, 0, sizeof(newSnap));

	// we will have read any new server commands in this
	// message before we got to svc_snapshot
	newSnap.serverCommandNum = clc.serverCommandSequence;

	newSnap.serverTime = MSG_ReadLong( msg );
	newSnap.serverTimeResidual = MSG_ReadByte( msg );

	newSnap.messageNum = clc.serverMessageSequence;

	deltaNum = MSG_ReadByte( msg );
	if ( !deltaNum ) {
		newSnap.deltaNum = -1;
	} else {
		newSnap.deltaNum = newSnap.messageNum - deltaNum;
	}
	newSnap.snapFlags = MSG_ReadByte( msg );

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message 
	if ( newSnap.deltaNum <= 0 ) {
		newSnap.valid = qtrue;		// uncompressed frame
		old = NULL;
		clc.demowaiting = qfalse;	// we can start recording now
	} else {
		old = &cl.snapshots[newSnap.deltaNum & PACKET_MASK];
		if ( !old->valid ) {
			// should never happen
			Com_Printf ("Delta from invalid frame (not supposed to happen!).\n");
		} else if ( old->messageNum != newSnap.deltaNum ) {
			// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_Printf ("Delta frame too old.\n");
		} else if ( cl.parseEntitiesNum - old->parseEntitiesNum > MAX_PARSE_ENTITIES-128 ) {
			Com_Printf ("Delta parseEntitiesNum too old.\n");
		} else {
			newSnap.valid = qtrue;	// valid delta parse
		}
	}

	// read areamask
	len = MSG_ReadByte( msg );
	
	if(len > sizeof(newSnap.areamask))
	{
		Com_Error (ERR_DROP,"CL_ParseSnapshot: Invalid size %d for areamask.", len);
		return;
	}
	
	MSG_ReadData( msg, &newSnap.areamask, len);

	// read playerinfo
    SHOWNET(msg, "playerstate");
	if ( old ) {
		MSG_ReadDeltaPlayerstate( msg, &old->ps, &newSnap.ps, cls.serverFrameTime );
	} else {
		MSG_ReadDeltaPlayerstate( msg, NULL, &newSnap.ps, cls.serverFrameTime);
	}
	// get normalized flags
	newSnap.ps.pm_flags = CPT_NormalizePlayerStateFlags(newSnap.ps.net_pm_flags);
	newSnap.ps.iViewModelAnim = CPT_NormalizeViewModelAnim(newSnap.ps.iNetViewModelAnim);

	// read packet entities
	SHOWNET( msg, "packet entities" );
	CL_ParsePacketEntities( msg, old, &newSnap );

	MSG_ReadSounds( msg, newSnap.sounds, &newSnap.number_of_sounds );

	// if not valid, dump the entire thing now that it has
	// been properly read
	if ( !newSnap.valid ) {
		return;
	}

	// clear the valid flags of any snapshots between the last
	// received and this one, so if there was a dropped packet
	// it won't look like something valid to delta from next
	// time we wrap around in the buffer
	oldMessageNum = cl.snap.messageNum + 1;

	if ( newSnap.messageNum - oldMessageNum >= PACKET_BACKUP ) {
		oldMessageNum = newSnap.messageNum - ( PACKET_BACKUP - 1 );
	}
	for ( ; oldMessageNum < newSnap.messageNum ; oldMessageNum++ ) {
		cl.snapshots[oldMessageNum & PACKET_MASK].valid = qfalse;
	}

	if( cl.snap.valid && ( newSnap.snapFlags ^ cl.snap.snapFlags ) & SNAPFLAG_SERVERCOUNT ) {
		cl.serverStartTime = newSnap.serverTime;
	}

	// copy to the current good spot
	cl.snap = newSnap;
	cl.snap.ping = 999;
	// calculate ping time
	for ( i = 0 ; i < PACKET_BACKUP ; i++ ) {
		packetNum = ( clc.netchan.outgoingSequence - 1 - i ) & PACKET_MASK;
		if ( cl.snap.ps.commandTime >= cl.outPackets[ packetNum ].p_serverTime ) {
			cl.snap.ping = cls.realtime - cl.outPackets[ packetNum ].p_realtime;
			break;
		}
	}
	// save the frame off in the backup array for later delta comparisons
	cl.snapshots[cl.snap.messageNum & PACKET_MASK] = cl.snap;

	if (cl_shownet->integer == 3) {
		Com_Printf( "   snapshot:%i  delta:%i  ping:%i\n", cl.snap.messageNum,
		cl.snap.deltaNum, cl.snap.ping );
	}

	cl.newSnapshots = qtrue;
	
	// read radar info about invisible clients
	CL_ReadNonPVSClient(cl.snap.ps.radarInfo);
}


//=====================================================================

int cl_connectedToPureServer;
int cl_connectedToCheatServer;

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged( void ) {
	char			*systemInfo;
	const char		*s, *t;
	char			key[BIG_INFO_KEY];
	char			value[BIG_INFO_VALUE];
	qboolean		gameSet;

	systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SYSTEMINFO ];
	// NOTE TTimo:
	// when the serverId changes, any further messages we send to the server will use this new serverId
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
	// in some cases, outdated cp commands might get sent with this news serverId
	cl.serverId = atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

#ifdef USE_VOIP
#ifdef LEGACY_PROTOCOL
	if(clc.compat)
		clc.voipEnabled = qfalse;
	else
#endif
	{
		s = Info_ValueForKey( systemInfo, "sv_voipProtocol" );
		clc.voipEnabled = !Q_stricmp(s, "opus");
	}
#endif

	// don't set any vars when playing a demo
	if ( clc.demoplaying ) {
		return;
	}

	s = Info_ValueForKey( systemInfo, "cheats" );
	cl_connectedToCheatServer = atoi( s );
	if ( !cl_connectedToCheatServer ) {
		Cvar_SetCheatState();
	}

	// check pure server string
	s = Info_ValueForKey( systemInfo, "sv_paks" );
	t = Info_ValueForKey( systemInfo, "sv_pakNames" );
	FS_PureServerSetLoadedPaks( s, t );

	s = Info_ValueForKey( systemInfo, "sv_referencedPaks" );
	t = Info_ValueForKey( systemInfo, "sv_referencedPakNames" );
	FS_PureServerSetReferencedPaks( s, t );

	gameSet = qfalse;
	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;
	while ( s ) {
		int cvar_flags;
		
		Info_NextPair( &s, key, value );
		if ( !key[0] ) {
			break;
		}
		
		// ehw!
		if (!Q_stricmp(key, "fs_game"))
		{
			if(FS_CheckDirTraversal(value))
			{
				Com_Printf(S_COLOR_YELLOW "WARNING: Server sent invalid fs_game value %s\n", value);
				continue;
			}
				
			gameSet = qtrue;
		}

		if((cvar_flags = Cvar_Flags(key)) == CVAR_NONEXISTENT)
			Cvar_Get(key, value, CVAR_SERVER_CREATED | CVAR_ROM);
		else
		{
			// If this cvar may not be modified by a server discard the value.
			if(!(cvar_flags & (CVAR_SYSTEMINFO | CVAR_SERVER_CREATED)))
			{
				Com_Printf(S_COLOR_YELLOW "WARNING: server is not allowed to set %s=%s\n", key, value);
				continue;
			}

			Cvar_Set(key, value);
		}
	}
	// if game folder should not be set and it is set at the client side
	if ( !gameSet && *Cvar_VariableString("fs_game") ) {
		Cvar_Set( "fs_game", "" );
	}
	// wombat: we ignore server's sv_pure for now
	//cl_connectedToPureServer = Cvar_VariableValue( "sv_pure" );
	cl_connectedToPureServer = 0;
}

/*
==================
CL_ParseServerInfo
==================
*/
static void CL_ParseServerInfo(void)
{
	const char *serverInfo;

	serverInfo = cl.gameState.stringData
		+ cl.gameState.stringOffsets[ CS_SERVERINFO ];

	clc.sv_allowDownload = atoi(Info_ValueForKey(serverInfo,
		"sv_allowDownload"));
	Q_strncpyz(clc.sv_dlURL,
		Info_ValueForKey(serverInfo, "sv_dlURL"),
		sizeof(clc.sv_dlURL));
}

/*
==================
CL_ParseGamestate
==================
*/
void CL_ParseGamestate( msg_t *msg ) {
	int				i, csNum;
	entityState_t	*es;
	int				newnum;
	entityState_t	nullstate;
	int				cmd;
	char			*s;
	cvar_t			*sv_paks;

	UI_CloseConsole();

	clc.connectPacketCount = 0;
	if (cls.cgameStarted) {
		CL_FlushMemory();
	}

	// wipe local client state
	CL_ClearState();

	// a gamestate always marks a server command sequence
	clc.serverCommandSequence = MSG_ReadLong( msg );

	// parse all the configstrings and baselines
	cl.gameState.dataCount = 1;	// leave a 0 at the beginning for uninitialized configstrings
	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}
		
		if ( cmd == svc_configstring ) {
            int		len;

            i = MSG_ReadShort(msg);
            csNum = CPT_NormalizeConfigstring(i);
            if (csNum < 0 || csNum >= MAX_CONFIGSTRINGS) {
                Com_Error(ERR_DROP, "configstring > MAX_CONFIGSTRINGS");
            }
            s = MSG_ReadScrambledBigString(msg);
            len = strlen(s);

            if (len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS) {
                Com_Error(ERR_DROP, "MAX_GAMESTATE_CHARS exceeded");
            }

            // append it to the gameState string buffer
            cl.gameState.stringOffsets[csNum] = cl.gameState.dataCount;
            Com_Memcpy(cl.gameState.stringData + cl.gameState.dataCount, s, len + 1);
            cl.gameState.dataCount += len + 1;
		} else if ( cmd == svc_baseline ) {
			newnum = MSG_ReadEntityNum(msg);
			if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
				Com_Error( ERR_DROP, "Baseline number out of range: %i", newnum );
			}
			//Com_Memset (&nullstate, 0, sizeof(nullstate));
			MSG_GetNullEntityState(&nullstate);
			es = &cl.entityBaselines[ newnum ];
			// FIXME: frametime
			MSG_ReadDeltaEntity( msg, &nullstate, es, newnum, 0.0);
		} else {
			Com_Error( ERR_DROP, "CL_ParseGamestate: bad command byte %i", cmd );
		}
	}

	clc.clientNum = MSG_ReadLong(msg);
	// read the checksum feed
	clc.checksumFeed = MSG_ReadLong( msg );
	cls.serverFrameTime = MSG_ReadServerFrameTime(msg, &cl.gameState);

	// parse serverId and other cvars
	CL_SystemInfoChanged();

	// stop recording now so the demo won't have an unnecessary level load at the end.
	if(cl_autoRecordDemo->integer && clc.demorecording)
		CL_StopRecord_f();

	if (clc.state == CA_CONNECTED && !Cvar_Get("sv_paks", "", 0)->string[0]) {
		// Added in 2.30
		FS_Restart(clc.checksumFeed);
	} else {
		// reinitialize the filesystem if the game directory has changed
		FS_ConditionalRestart(clc.checksumFeed, qfalse);
	}

	clc.state = CA_LOADING;
    if (!com_sv_running->integer)
    {
        const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		const char *mapname = Info_ValueForKey(info, "mapname");
		// Added in 2.0
		Cvar_Set("mapname", mapname);

		UI_ClearState();
        UI_BeginLoad(mapname);
    }

	// This used to call CL_StartHunkUsers, but now we enter the download state before loading the
	// cgame
	CL_InitDownloads();

	// make sure the game starts
	Com_Unpause();
}


//=====================================================================

int CL_MSG_ReadBits( int bits )
{
	return MSG_ReadBits( cl_currentMSG, bits );
}

int CL_MSG_ReadChar( void )
{
	return MSG_ReadChar( cl_currentMSG );
}

int CL_MSG_ReadByte( void )
{
	return MSG_ReadByte( cl_currentMSG );
}

int CL_MSG_ReadSVC( void )
{
	return MSG_ReadSVC( cl_currentMSG );
}

int CL_MSG_ReadShort( void )
{
	return MSG_ReadShort( cl_currentMSG );
}

int CL_MSG_ReadLong( void )
{
	return MSG_ReadLong( cl_currentMSG );
}

float CL_MSG_ReadFloat( void )
{
	return MSG_ReadFloat( cl_currentMSG );
}

char *CL_MSG_ReadString( void )
{
	return MSG_ReadString( cl_currentMSG );
}

char *CL_MSG_ReadStringLine( void )
{
	return MSG_ReadStringLine( cl_currentMSG );
}

float CL_MSG_ReadAngle8( void )
{
	return MSG_ReadAngle8( cl_currentMSG );
}

float CL_MSG_ReadAngle16( void )
{
	return MSG_ReadAngle16( cl_currentMSG );
}

void CL_MSG_ReadData( void *data, int len )
{
	return MSG_ReadData( cl_currentMSG, data, len );
}

float CL_MSG_ReadCoord( void )
{
	return MSG_ReadCoord( cl_currentMSG );
}

void CL_MSG_ReadDir( vec3_t dir )
{
	MSG_ReadDir( cl_currentMSG, dir );
}

/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/
void CL_ParseDownload ( msg_t *msg ) {
	int		size;
	unsigned char data[MAX_MSGLEN];
	int block;

	if (!*clc.downloadTempName) {
		Com_Printf("Server sending download, but no download was requested\n");
		CL_AddReliableCommand( "stopdl", qfalse );
		return;
	}

	// read the data
	block = MSG_ReadShort ( msg );

	if ( !block )
	{
		// block zero is special, contains file size
		clc.downloadSize = MSG_ReadLong ( msg );

		Cvar_SetValue( "cl_downloadSize", clc.downloadSize );

		if (clc.downloadSize < 0)
		{
			Com_Error( ERR_DROP, "%s", MSG_ReadString( msg ) );
			return;
		}
	}

	size = MSG_ReadShort ( msg );
	if (size < 0 || size > sizeof(data))
	{
		Com_Error(ERR_DROP, "CL_ParseDownload: Invalid size %d for download chunk.", size);
		return;
	}
	
	MSG_ReadData(msg, data, size);

	if (clc.downloadBlock != block) {
		Com_DPrintf( "CL_ParseDownload: Expected block %d, got %d\n", clc.downloadBlock, block);
		return;
	}

	// open the file if not opened yet
	if (!clc.download)
	{
		clc.download = FS_SV_FOpenFileWrite( clc.downloadTempName );

		if (!clc.download) {
			Com_Printf( "Could not create %s\n", clc.downloadTempName );
			CL_AddReliableCommand( "stopdl", qfalse );
			CL_NextDownload();
			return;
		}
	}

	if (size)
		FS_Write( data, size, clc.download );

	CL_AddReliableCommand( va("nextdl %d", clc.downloadBlock), qfalse );
	clc.downloadBlock++;

	clc.downloadCount += size;

	// So UI gets access to it
	Cvar_SetValue( "cl_downloadCount", clc.downloadCount );

	if (!size) { // A zero length block means EOF
		if (clc.download) {
			FS_FCloseFile( clc.download );
			clc.download = 0;

			// rename the file
			FS_SV_Rename ( clc.downloadTempName, clc.downloadName, qfalse );
		}
		*clc.downloadTempName = *clc.downloadName = 0;
		Cvar_Set( "cl_downloadName", "" );

		// send intentions now
		// We need this because without it, we would hold the last nextdl and then start
		// loading right away.  If we take a while to load, the server is happily trying
		// to send us that last block over and over.
		// Write it twice to help make sure we acknowledge the download
		CL_WritePacket();
		CL_WritePacket();

		// get another file if needed
		CL_NextDownload ();
	}
}

#ifdef USE_VOIP
static
qboolean CL_ShouldIgnoreVoipSender(int sender)
{
	if (!cl_voip->integer)
		return qtrue;  // VoIP is disabled.
	else if ((sender == clc.clientNum) && (!clc.demoplaying))
		return qtrue;  // ignore own voice (unless playing back a demo).
	else if (clc.voipMuteAll)
		return qtrue;  // all channels are muted with extreme prejudice.
	else if (clc.voipIgnore[sender])
		return qtrue;  // just ignoring this guy.
	else if (clc.voipGain[sender] == 0.0f)
		return qtrue;  // too quiet to play.

	return qfalse;
}

/*
=====================
CL_PlayVoip

Play raw data
=====================
*/

static void CL_PlayVoip(int sender, int samplecnt, const byte *data, int flags)
{
	if(flags & VOIP_DIRECT)
	{
		S_RawSamples(sender + 1, samplecnt, 48000, 2, 1,
	             data, clc.voipGain[sender], -1);
	}

	if(flags & VOIP_SPATIAL)
	{
		S_RawSamples(sender + MAX_CLIENTS + 1, samplecnt, 48000, 2, 1,
	             data, 1.0f, sender);
	}
}

/*
=====================
CL_ParseVoip

A VoIP message has been received from the server
=====================
*/
static
void CL_ParseVoip ( msg_t *msg, qboolean ignoreData ) {
	static short decoded[VOIP_MAX_PACKET_SAMPLES*4]; // !!! FIXME: don't hard code

	const int sender = MSG_ReadShort(msg);
	const int generation = MSG_ReadByte(msg);
	const int sequence = MSG_ReadLong(msg);
	const int frames = MSG_ReadByte(msg);
	const int packetsize = MSG_ReadShort(msg);
	const int flags = MSG_ReadBits(msg, VOIP_FLAGCNT);
	unsigned char encoded[4000];
	int	numSamples;
	int seqdiff;
	int written = 0;
	int i;

	Com_DPrintf("VoIP: %d-byte packet from client %d\n", packetsize, sender);

	if (sender < 0)
		return;   // short/invalid packet, bail.
	else if (generation < 0)
		return;   // short/invalid packet, bail.
	else if (sequence < 0)
		return;   // short/invalid packet, bail.
	else if (frames < 0)
		return;   // short/invalid packet, bail.
	else if (packetsize < 0)
		return;   // short/invalid packet, bail.

	if (packetsize > sizeof (encoded)) {  // overlarge packet?
		int bytesleft = packetsize;
		while (bytesleft) {
			int br = bytesleft;
			if (br > sizeof (encoded))
				br = sizeof (encoded);
			MSG_ReadData(msg, encoded, br);
			bytesleft -= br;
		}
		return;   // overlarge packet, bail.
	}

	MSG_ReadData(msg, encoded, packetsize);

	if (ignoreData) {
		return; // just ignore legacy speex voip data
	} else if (!clc.voipCodecInitialized) {
		return;   // can't handle VoIP without libopus!
	} else if (sender >= MAX_CLIENTS) {
		return;   // bogus sender.
	} else if (CL_ShouldIgnoreVoipSender(sender)) {
		return;   // Channel is muted, bail.
	}

	// !!! FIXME: make sure data is narrowband? Does decoder handle this?

	Com_DPrintf("VoIP: packet accepted!\n");

	seqdiff = sequence - clc.voipIncomingSequence[sender];

	// This is a new "generation" ... a new recording started, reset the bits.
	if (generation != clc.voipIncomingGeneration[sender]) {
		Com_DPrintf("VoIP: new generation %d!\n", generation);
		opus_decoder_ctl(clc.opusDecoder[sender], OPUS_RESET_STATE);
		clc.voipIncomingGeneration[sender] = generation;
		seqdiff = 0;
	} else if (seqdiff < 0) {   // we're ahead of the sequence?!
		// This shouldn't happen unless the packet is corrupted or something.
		Com_DPrintf("VoIP: misordered sequence! %d < %d!\n",
		            sequence, clc.voipIncomingSequence[sender]);
		// reset the decoder just in case.
		opus_decoder_ctl(clc.opusDecoder[sender], OPUS_RESET_STATE);
		seqdiff = 0;
	} else if (seqdiff * VOIP_MAX_PACKET_SAMPLES*2 >= sizeof (decoded)) { // dropped more than we can handle?
		// just start over.
		Com_DPrintf("VoIP: Dropped way too many (%d) frames from client #%d\n",
		            seqdiff, sender);
		opus_decoder_ctl(clc.opusDecoder[sender], OPUS_RESET_STATE);
		seqdiff = 0;
	}

	if (seqdiff != 0) {
		Com_DPrintf("VoIP: Dropped %d frames from client #%d\n",
		            seqdiff, sender);
		// tell opus that we're missing frames...
		for (i = 0; i < seqdiff; i++) {
			assert((written + VOIP_MAX_PACKET_SAMPLES) * 2 < sizeof (decoded));
			numSamples = opus_decode(clc.opusDecoder[sender], NULL, 0, decoded + written, VOIP_MAX_PACKET_SAMPLES, 0);
			if ( numSamples <= 0 ) {
				Com_DPrintf("VoIP: Error decoding frame %d from client #%d\n", i, sender);
				continue;
			}
			written += numSamples;
		}
	}

	numSamples = opus_decode(clc.opusDecoder[sender], encoded, packetsize, decoded + written, ARRAY_LEN(decoded) - written, 0);

	if ( numSamples <= 0 ) {
		Com_DPrintf("VoIP: Error decoding voip data from client #%d\n", sender);
		numSamples = 0;
	}

	#if 0
	static FILE *encio = NULL;
	if (encio == NULL) encio = fopen("voip-incoming-encoded.bin", "wb");
	if (encio != NULL) { fwrite(encoded, packetsize, 1, encio); fflush(encio); }
	static FILE *decio = NULL;
	if (decio == NULL) decio = fopen("voip-incoming-decoded.bin", "wb");
	if (decio != NULL) { fwrite(decoded+written, numSamples*2, 1, decio); fflush(decio); }
	#endif

	written += numSamples;

	Com_DPrintf("VoIP: playback %d bytes, %d samples, %d frames\n",
	            written * 2, written, frames);

	if(written > 0)
		CL_PlayVoip(sender, written, (const byte *) decoded, flags);

	clc.voipIncomingSequence[sender] = sequence + frames;
}
#endif


/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
void CL_ParseCommandString( msg_t *msg ) {
	char	*s;
	int		seq;
	int		index;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadScrambledString( msg );

	// see if we have already executed stored it off
	if ( clc.serverCommandSequence >= seq ) {
		return;
	}
	clc.serverCommandSequence = seq;

	index = seq & (MAX_RELIABLE_COMMANDS-1);
	Q_strncpyz( clc.serverCommands[ index ], s, sizeof( clc.serverCommands[ index ] ) );
}

/*
=====================
CL_ParseCGMessage

MOHAA does this inside its cgame. its some big function with a 37-switch case
but unless we properly read the CG message, we don't know when the message
hase finished
=====================
*/
void CL_ParseCGMessage( msg_t *msg ) {
	cl_currentMSG = msg;
	cge->CG_ParseCGMessage();
}

/*
=====================
CL_ParseLocationprint
=====================
*/
void CL_ParseLocationprint( msg_t *msg ) {
	int x, y;
	char *string;

	x = MSG_ReadShort( msg );
	y = MSG_ReadShort( msg );
	string = MSG_ReadScrambledString(msg);

	UI_UpdateLocationPrint( x, y, string, 1.0 );
}

/*
=====================
CL_ParseCenterprint
=====================
*/
void CL_ParseCenterprint( msg_t *msg ) {
	char *string;

	string = MSG_ReadScrambledString( msg );

	// FIXME
	UI_UpdateCenterPrint( string, 1.0 );
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( msg_t *msg ) {
	int			cmd;
//Com_Printf( "ParseServerMessage: %i\n", msg->cursize );
	if ( cl_shownet->integer == 1 ) {
		Com_Printf ("%zu ",msg->cursize);
	} else if ( cl_shownet->integer >= 2 ) {
		Com_Printf ("------------------\n");
	}

	MSG_Bitstream(msg);

	// get the reliable sequence acknowledge number
	clc.reliableAcknowledge = MSG_ReadLong( msg );
	// 
	if ( clc.reliableAcknowledge < clc.reliableSequence - MAX_RELIABLE_COMMANDS ) {
		clc.reliableAcknowledge = clc.reliableSequence;
	}

	//
	// parse the message
	//
	while ( 1 ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Error (ERR_DROP,"CL_ParseServerMessage: read past end of server message");
			break;
		}

		cmd = MSG_ReadByte( msg );
	//	Com_Printf( "CL_ParseServerMessage: cmd %i\n", cmd );
		if ( cmd == svc_EOF) {
			SHOWNET( msg, "END OF MESSAGE" );
			break;
		}

		if ( cl_shownet->integer >= 2 ) {
			if ( !svc_strings[cmd] ) {
				Com_Printf( "%3i:BAD CMD %i\n", msg->readcount-1, cmd );
			} else {
				SHOWNET( msg, svc_strings[cmd] );
			}
		}
	
	// other commands
		switch ( cmd ) {
		default:
			Com_Error (ERR_DROP,"CL_ParseServerMessage: Illegible server message\n");
			break;			
		case svc_nop:
			break;
		case svc_serverCommand:
			CL_ParseCommandString( msg );
			break;
		case svc_gamestate:
			CL_ParseGamestate( msg );
			break;
		case svc_snapshot:
			CL_ParseSnapshot( msg );
			break;
		case svc_download:
			CL_ParseDownload( msg );
			break;
		case svc_centerprint:
			CL_ParseCenterprint( msg );
			break;
		case svc_locprint:
			CL_ParseLocationprint( msg );
			break;
		case svc_cgameMessage:
			if( !cge ) {
				Com_Error(ERR_DROP,"CL_ParseServerMessage: tried to parse cg message without cgame loaded\n");
			}
			CL_ParseCGMessage( msg );
			break;
		case svc_voipSpeex:
#ifdef USE_VOIP
			CL_ParseVoip( msg, qtrue );
#endif
			break;
		case svc_voipOpus:
#ifdef USE_VOIP
			CL_ParseVoip( msg, !clc.voipEnabled );
#endif
			break;
		}
	}
}
