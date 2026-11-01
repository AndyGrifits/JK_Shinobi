/*
===========================================================================
JK_Shinobi - kunai and what is built on them.

  Z            throw a kunai (everyone)
  Shift + Z    throw a marked kunai (Hiraishin seal, 10 chakra): stays 30 s;
               if it hits an enemy, that enemy carries the mark for 12 s
  Hiraishin    (Yin/Yang jutsu) teleport to your newest mark - behind the
               marked enemy if it is on one
  Lightning Trap (Raiton jutsu) every mark of yours discharges: damage and a
               short stun around it

Also here: Shift + attack infuses fists, kicks and the blade with chakra.
===========================================================================
*/
#include "g_shinobi_local.h"

#define KUNAI_MARKED(k)    ( (k)->genericValue2 )
#define KUNAI_EXPIRE(k)    ( (k)->genericValue1 )
#define KUNAI_ON(k)        ( (k)->genericValue3 )  // marked enemy num + 1
#define KUNAI_STUCK(k)     ( (k)->genericValue4 )
#define KUNAI_BORN(k)      ( (k)->genericValue5 )

static int kunaiModel, sndThrow, sndStick, sndFlash;
static int fxFlash, fxFireBody;

void Shinobi_KunaiInit( void ) {
	kunaiModel = G_ModelIndex( SHINOBI_KUNAI_MODEL );
	sndThrow   = G_SoundIndex( "sound/weapons/saber/saberhup1.wav" );
	sndStick   = G_SoundIndex( "sound/weapons/saber/saberhitwall1" );
	sndFlash   = G_SoundIndex( "sound/weapons/force/speed.wav" );
	fxFlash    = G_EffectIndex( SHINOBI_FX_HIRAISHIN );
	fxFireBody = G_EffectIndex( SHINOBI_FX_FIRE_BODY );
}

static qboolean Shinobi_IsMark( gentity_t *k, int ownerNum ) {
	return ( k->inuse && k->classname && !strcmp( k->classname, "shinobi_kunai" ) && KUNAI_MARKED( k )
		&& k->r.ownerNum == ownerNum ) ? qtrue : qfalse;
}

static void Shinobi_KunaiStick( gentity_t *k, vec3_t pos, vec3_t normal ) {
	vec3_t dir;

	VectorCopy( k->s.pos.trDelta, dir );
	VectorNormalize( dir );
	VectorMA( pos, 3, dir, pos ); // bite into the surface
	G_SetOrigin( k, pos );
	VectorCopy( normal, k->movedir );
	KUNAI_STUCK( k ) = 1;
	KUNAI_EXPIRE( k ) = level.time + ( KUNAI_MARKED( k ) ? SHINOBI_MARK_LIFE : SHINOBI_KUNAI_LIFE );
	G_SoundAtLoc( pos, CHAN_AUTO, sndStick );
	trap->LinkEntity( (sharedEntity_t *)k );
}

static void Shinobi_KunaiThink( gentity_t *k ) {
	gentity_t *owner = &g_entities[k->r.ownerNum];
	vec3_t     newPos, vel, mins = { -2, -2, -2 }, maxs = { 2, 2, 2 };
	trace_t    tr;

	if ( level.time >= KUNAI_EXPIRE( k ) ) {
		G_FreeEntity( k );
		return;
	}

	if ( KUNAI_ON( k ) ) { // riding on the enemy it marked
		gentity_t *t = &g_entities[KUNAI_ON( k ) - 1];
		vec3_t     pos;
		if ( !t->inuse || !t->client || t->health <= 0 ) {
			G_FreeEntity( k );
			return;
		}
		VectorCopy( t->r.currentOrigin, pos );
		pos[2] += 6;
		G_SetOrigin( k, pos );
		trap->LinkEntity( (sharedEntity_t *)k );
		k->nextthink = level.time + FRAMETIME;
		return;
	}
	if ( KUNAI_STUCK( k ) ) {
		k->nextthink = KUNAI_EXPIRE( k );
		return;
	}

	// in flight: point first along the curve
	BG_EvaluateTrajectory( &k->s.pos, level.time, newPos );
	BG_EvaluateTrajectoryDelta( &k->s.pos, level.time, vel );
	vectoangles( vel, k->s.apos.trBase );
	VectorCopy( k->s.apos.trBase, k->r.currentAngles );

	trap->Trace( &tr, k->r.currentOrigin, mins, maxs, newPos, k->r.ownerNum, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		gentity_t *other = ( tr.entityNum < ENTITYNUM_WORLD ) ? &g_entities[tr.entityNum] : NULL;
		vec3_t     dir;

		VectorCopy( vel, dir );
		VectorNormalize( dir );
		if ( other && other->takedamage && ( !owner->inuse || !Shinobi_DamageBlocked( other, owner ) ) ) {
			G_Damage( other, k, owner->inuse ? owner : k, dir, tr.endpos, SHINOBI_KUNAI_DAMAGE, 0, MOD_SABER );
			if ( KUNAI_MARKED( k ) && other->inuse && other->client && other->s.number < MAX_CLIENTS && other->health > 0 ) {
				KUNAI_ON( k ) = other->s.number + 1;
				KUNAI_EXPIRE( k ) = level.time + SHINOBI_MARK_ON_ENEMY;
				k->s.pos.trType = TR_STATIONARY;
				if ( owner->inuse )
					trap->SendServerCommand( owner - g_entities, "cp \"^3Marked!\"" );
				k->nextthink = level.time + FRAMETIME;
				return;
			}
			G_FreeEntity( k );
			return;
		}
		if ( other && other->client ) { // a body we may not hurt (own clone): drop it
			G_FreeEntity( k );
			return;
		}
		Shinobi_KunaiStick( k, tr.endpos, tr.plane.normal );
		k->nextthink = KUNAI_EXPIRE( k );
		return;
	}

	VectorCopy( newPos, k->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)k );
	k->nextthink = level.time + FRAMETIME;
}

void Cmd_Kunai_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	const qboolean marked = SHINOBI_MODIFIER( &client->pers.cmd );
	gentity_t *k;
	vec3_t     fwd, eye, start;
	trace_t    tr;
	int        i;

	if ( !Shinobi_CanAct( ent ) || client->shinobi.prisonCaster || client->shinobi.shadowBy
		|| client->shinobi.stunEnd > level.time || SHINOBI_HAND_ANY( &client->ps ) )
		return;
	if ( client->shinobi.kunaiNext > level.time )
		return;
	if ( marked && !Shinobi_SpendChakra( ent, SHINOBI_MARK_COST ) )
		return;
	Shinobi_Reveal( ent );
	client->shinobi.kunaiNext = level.time + SHINOBI_KUNAI_COOLDOWN;

	if ( marked ) { // only so many marks: the oldest goes
		gentity_t *oldest = NULL;
		int        count = 0;
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			gentity_t *e = &g_entities[i];
			if ( !Shinobi_IsMark( e, ent->s.number ) )
				continue;
			count++;
			if ( !oldest || KUNAI_BORN( e ) < KUNAI_BORN( oldest ) )
				oldest = e;
		}
		if ( count >= SHINOBI_MARK_MAX && oldest )
			G_FreeEntity( oldest );
	}

	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( client->ps.origin, eye );
	eye[2] += client->ps.viewheight;
	VectorMA( eye, 16, fwd, start );
	trap->Trace( &tr, eye, NULL, NULL, start, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorCopy( tr.endpos, start );

	k = G_Spawn();
	if ( !k )
		return;
	k->classname = "shinobi_kunai";
	k->s.eType = ET_GENERAL;
	k->s.modelindex = kunaiModel;
	k->s.generic1 = marked ? SHINOBI_ENT_KUNAI_MARKED : SHINOBI_ENT_KUNAI;
	k->r.ownerNum = ent->s.number;
	k->parent = ent;
	k->r.contents = 0;
	VectorSet( k->r.mins, -2, -2, -2 );
	VectorSet( k->r.maxs, 2, 2, 2 );
	KUNAI_MARKED( k ) = marked;
	KUNAI_BORN( k ) = level.time;
	KUNAI_EXPIRE( k ) = level.time + 3000;
	k->s.pos.trType = TR_GRAVITY;
	k->s.pos.trTime = level.time;
	VectorCopy( start, k->s.pos.trBase );
	VectorScale( fwd, SHINOBI_KUNAI_SPEED, k->s.pos.trDelta );
	k->s.pos.trDelta[2] += 40;
	VectorCopy( start, k->r.currentOrigin );
	k->s.apos.trType = TR_STATIONARY;
	vectoangles( fwd, k->s.apos.trBase );
	k->think = Shinobi_KunaiThink;
	k->nextthink = level.time + FRAMETIME;
	trap->LinkEntity( (sharedEntity_t *)k );

	client->ps.forceHandExtend = HANDEXTEND_FORCEPUSH;
	client->ps.forceHandExtendTime = level.time + 250;
	G_Sound( ent, CHAN_WEAPON, sndThrow );
}

// newest mark of this player that is in place (stuck or on an enemy)
static gentity_t *Shinobi_NewestMark( gentity_t *ent ) {
	gentity_t *best = NULL;
	int        i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !Shinobi_IsMark( e, ent->s.number ) || ( !KUNAI_STUCK( e ) && !KUNAI_ON( e ) ) )
			continue;
		if ( !best || KUNAI_BORN( e ) > KUNAI_BORN( best ) )
			best = e;
	}
	return best;
}

// is there room to stand at 'spot'?
static qboolean Shinobi_RoomAt( gentity_t *ent, vec3_t spot ) {
	trace_t tr;
	trap->Trace( &tr, spot, ent->r.mins, ent->r.maxs, spot, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	return ( tr.startsolid || tr.allsolid ) ? qfalse : qtrue;
}

qboolean Shinobi_CastHiraishin( gentity_t *ent ) {
	gclient_t *client = ent->client;
	gentity_t *mark = Shinobi_NewestMark( ent );
	vec3_t     dest, look, angles;
	qboolean   found = qfalse;
	int        tries;

	if ( client->shinobi.hiraishinNext > level.time ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, va( "cp \"Hiraishin ready in %.1f s\"",
			( client->shinobi.hiraishinNext - level.time ) / 1000.0f ) );
		return qfalse;
	}
	if ( !mark ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No marked kunai (Shift+Z to throw one)\"" );
		return qfalse;
	}

	if ( KUNAI_ON( mark ) ) { // behind the marked enemy, facing him
		gentity_t *t = &g_entities[KUNAI_ON( mark ) - 1];
		static const float around[] = { 180, 135, -135, 90, -90, 0 };
		vec3_t fwd;
		for ( tries = 0; tries < (int)ARRAY_LEN( around ) && !found; tries++ ) {
			VectorSet( angles, 0, t->client->ps.viewangles[YAW] + around[tries], 0 );
			AngleVectors( angles, fwd, NULL, NULL );
			VectorMA( t->r.currentOrigin, 48, fwd, dest );
			found = Shinobi_RoomAt( ent, dest );
		}
		VectorSubtract( t->r.currentOrigin, dest, look );
	}
	else { // next to the kunai, out of the wall it is stuck in
		vec3_t base;
		VectorMA( mark->r.currentOrigin, 24, mark->movedir, base );
		for ( tries = 0; tries < 4 && !found; tries++ ) {
			VectorCopy( base, dest );
			dest[2] += tries * 18 + ( mark->movedir[2] > 0.6f ? 24 : 0 );
			found = Shinobi_RoomAt( ent, dest );
		}
		AngleVectors( client->ps.viewangles, look, NULL, NULL );
	}
	if ( !found ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No room by the mark\"" );
		return qfalse;
	}

	client->shinobi.hiraishinNext = level.time + SHINOBI_HIRAISHIN_COOLDOWN;
	Shinobi_StopCharging( client );
	{
		vec3_t up = { 0, 0, 1 };
		G_PlayEffectID( fxFlash, client->ps.origin, up );
		G_SoundAtLoc( client->ps.origin, CHAN_AUTO, sndFlash );
		Shinobi_TeleportClient( ent, dest );
		G_PlayEffectID( fxFlash, client->ps.origin, up );
		G_Sound( ent, CHAN_AUTO, sndFlash );
	}
	vectoangles( look, angles );
	angles[PITCH] = 0;
	angles[ROLL] = 0;
	SetClientViewAngle( ent, angles );
	return qtrue;
}

qboolean Shinobi_CastRaiTrap( gentity_t *ent ) {
	int i, n = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *k = &g_entities[i];
		vec3_t     pos, up = { 0, 0, 1 };
		int        j;

		if ( !Shinobi_IsMark( k, ent->s.number ) || ( !KUNAI_STUCK( k ) && !KUNAI_ON( k ) ) )
			continue;
		VectorCopy( k->r.currentOrigin, pos );

		for ( j = 0; j < level.num_entities; j++ ) {
			gentity_t *v = &g_entities[j];
			trace_t    tr;
			if ( !v->inuse || !v->client || v == ent || v->health <= 0 )
				continue;
			if ( Shinobi_DamageBlocked( v, ent ) || ( level.gametype >= GT_TEAM && OnSameTeam( ent, v ) ) )
				continue;
			if ( Distance( v->r.currentOrigin, pos ) > SHINOBI_RAITRAP_RADIUS )
				continue;
			trap->Trace( &tr, pos, NULL, NULL, v->r.currentOrigin, k->s.number, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f && tr.entityNum != v->s.number && !( KUNAI_ON( k ) && j == KUNAI_ON( k ) - 1 ) )
				continue;
			shinobiHitElement = SE_LIGHTNING;
			G_Damage( v, k, ent, NULL, v->r.currentOrigin, SHINOBI_RAITRAP_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_DEMP2 );
			shinobiHitElement = SE_NONE;
			if ( v->inuse && v->health > 0 && j < MAX_CLIENTS && !Shinobi_CCImmune( v ) ) {
				v->client->ps.electrifyTime = level.time + SHINOBI_RAITRAP_STUN_MS;
				if ( v->client->ps.weaponTime < SHINOBI_RAITRAP_STUN_MS )
					v->client->ps.weaponTime = SHINOBI_RAITRAP_STUN_MS;
				v->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
				v->client->ps.pm_time = SHINOBI_RAITRAP_STUN_MS / 2;
				Shinobi_CCGrant( v, level.time + SHINOBI_RAITRAP_STUN_MS );
			}
		}
		Shinobi_ElectrifyAt( pos, ent->s.number + 1 );
		Shinobi_EffectDir( shinobiMedia.fxChidoriHit[1], pos, up );
		G_SoundAtLoc( pos, CHAN_AUTO, shinobiMedia.sndChidoriHit );
		G_FreeEntity( k );
		n++;
	}
	if ( !n ) {
		G_Sound( ent, CHAN_AUTO, shinobiMedia.sndFizzle );
		trap->SendServerCommand( ent - g_entities, "cp \"No marked kunai to discharge (Shift+Z)\"" );
		return qfalse;
	}
	return qtrue;
}

/*
===========================================================================
Shift + attack: chakra in the weapon
===========================================================================
*/
void Shinobi_InfuseThink( gentity_t *ent, usercmd_t *ucmd ) {
	if ( SHINOBI_MODIFIER( ucmd ) && ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_ALT_ATTACK ) )
		&& !SHINOBI_HAND_ANY( &ent->client->ps ) )
		ent->client->shinobi.infuseUntil = level.time + SHINOBI_INFUSE_WINDOW_MS;
}

// called from Shinobi_BuildDamage for a fist, kick or blade hit
int Shinobi_InfuseHit( gentity_t *targ, gentity_t *attacker, int damage ) {
	gclient_t *ac = attacker->client;
	vec3_t     up = { 0, 0, 1 }, dir;

	if ( ac->shinobi.infuseUntil <= level.time || ac->ps.fd.forcePower < SHINOBI_INFUSE_COST || !targ || targ == attacker )
		return damage;
	ac->ps.fd.forcePower -= SHINOBI_INFUSE_COST;

	VectorSubtract( targ->r.currentOrigin, attacker->r.currentOrigin, dir );
	dir[2] = 0;
	VectorNormalize( dir );

	switch ( Shinobi_BuildElement( &ac->pers.shinobiBuild ) ) {
	case SS_KATON:
		Shinobi_Ignite( targ, attacker->s.number + 1 );
		G_PlayEffectID( fxFireBody, targ->r.currentOrigin, up );
		break;
	case SS_SUITON:
		Shinobi_Wet( targ );
		damage = damage * 115 / 100;
		Shinobi_EffectDir( shinobiMedia.fxWaterSplash[0], targ->r.currentOrigin, dir );
		break;
	case SS_FUTON:
		damage = damage * 140 / 100; // a blade of wind on the edge
		Shinobi_EffectDir( shinobiMedia.fxWindGust[0], targ->r.currentOrigin, dir );
		break;
	case SS_DOTON:
		damage = damage * 120 / 100;
		if ( targ->client ) { // heavy as a rock: knocked back
			VectorMA( targ->client->ps.velocity, 350, dir, targ->client->ps.velocity );
			targ->client->ps.velocity[2] += 120;
			targ->client->ps.groundEntityNum = ENTITYNUM_NONE;
		}
		G_PlayEffectID( shinobiMedia.fxWallBreak, targ->r.currentOrigin, up );
		break;
	case SS_RAITON:
		if ( targ->client && targ->s.number < MAX_CLIENTS && !Shinobi_CCImmune( targ ) ) {
			targ->client->ps.electrifyTime = level.time + 300;
			if ( targ->client->ps.weaponTime < 300 )
				targ->client->ps.weaponTime = 300;
			Shinobi_CCGrant( targ, level.time + 300 );
		}
		if ( targ->classname && !strcmp( targ->classname, "shinobi_earthwall" ) )
			damage *= 2; // lightning cuts through earth
		Shinobi_EffectDir( shinobiMedia.fxChidoriHit[0], targ->r.currentOrigin, dir );
		break;
	default:
		damage = damage * 125 / 100;
		Shinobi_EffectDir( shinobiMedia.fxRasenganHit[0], targ->r.currentOrigin, dir );
		break;
	}
	return damage;
}
