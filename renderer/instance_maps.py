#!/usr/bin/env python3
"""Dungeon and raid maps of the optional instance world cache: the six installer groups, their selection
syntax and the WDT/tile helpers. Pure Python; the names are the Map.dbc Directory column exactly (case and
punctuation kept: the cache folder, the .fgl file and the runtime lookup all use them verbatim).

Left out on purpose: Monastery (44), Sunwell5Man (598) and EmeraldDream (169) are unused maps; test and
development maps; battlegrounds, arenas and DeathKnightStart (open-air maps, a later group).
Naxxramas (Stratholme Raid) is a Wrath raid, Onyxia's Lair a Classic raid.
"""
from __future__ import annotations
import math, re, struct

# est_gb is new disk on top of a continent cache (map folders plus the model files a continent build lacks), est_minutes
# the scene build CPU time of the group; both measured from the stock client (about 3.8 GB and 10 CPU minutes for all six)
GROUPS=[
    {'key':'classic-dungeons','label':'Classic dungeons','est_gb':.6,'est_minutes':1.7,'maps':[
        'Shadowfang','StormwindJail','DeadminesInstance','WailingCaverns','RazorfenKraulInstance','Blackfathom','Uldaman',
        'GnomeragonInstance','SunkenTemple','RazorfenDowns','MonasteryInstances','TanarisInstance','BlackRockSpire',
        'BlackrockDepths','SchoolofNecromancy','Stratholme','Mauradon','OrgrimmarInstance','DireMaul']},
    {'key':'classic-raids','label':'Classic raids','est_gb':.3,'est_minutes':.9,'maps':[
        'OnyxiaLairInstance',"Zul'gurub",'MoltenCore','BlackwingLair','AhnQiraj','AhnQirajTemple']},
    {'key':'tbc-dungeons','label':'TBC dungeons','est_gb':.6,'est_minutes':1.8,'maps':[
        'HellfireMilitary','HellfireDemon','HellfireRampart','CoilfangPumping','CoilfangMarsh','CoilfangDraenei',
        'TempestKeepArcane','TempestKeepAtrium','TempestKeepFactory','AuchindounShadow','AuchindounDemon',
        'AuchindounEthereal','AuchindounDraenei','HillsbradPast','CavernsOfTime','Sunwell5ManFix']},
    {'key':'tbc-raids','label':'TBC raids','est_gb':.4,'est_minutes':1.3,'maps':[
        'Karazahn','HyjalPast','HellfireRaid','CoilfangRaid','TempestKeepRaid','BlackTemple','GruulsLair','ZulAman',
        'SunwellPlateau']},
    {'key':'wotlk-dungeons','label':'Wrath dungeons','est_gb':1.3,'est_minutes':2.3,'maps':[
        'Valgarde70','UtgardePinnacle','Nexus70','Nexus80','StratholmeCOT','Ulduar70','DrakTheronKeep','Azjol_Uppercity',
        'Ulduar80','GunDrak','DalaranPrison','Azjol_LowerCity','IcecrownCitadel5Man','ArgentTournamentDungeon',
        'QuarryofTears','HallsOfReflection']},
    {'key':'wotlk-raids','label':'Wrath raids','est_gb':.7,'est_minutes':1.9,'maps':[
        'Stratholme Raid','UlduarRaid','ChamberOfAspectsBlack','NexusRaid','WintergraspRaid','IcecrownCitadel',
        'ArgentTournamentRaid','ChamberofAspectsRed']},
]
KEYS=[g['key'] for g in GROUPS]
ALL_MAPS=[m for g in GROUPS for m in g['maps']]
_GROUP_OF={m:g['key'] for g in GROUPS for m in g['maps']}

def group_of(map_name):
    """The group key a map belongs to, or None."""
    return _GROUP_OF.get(map_name)

def maps_for(keys):
    """The maps of the given group keys, in GROUPS order, without duplicates."""
    wanted=set(keys);unknown=wanted-set(KEYS)
    if unknown:raise ValueError('Unknown instance group: '+', '.join(sorted(unknown)))
    return [m for g in GROUPS if g['key'] in wanted for m in g['maps']]

def resolve(spec):
    """Group keys (in GROUPS order, once each) for a selection: None, '' or 'none' -> []; 'all' -> every key;
    otherwise numbers 1-6 and/or keys separated by commas and/or spaces. ValueError names a bad token."""
    if spec is None:return []
    tokens=[t for t in re.split(r'[,\s]+',str(spec).strip().lower()) if t]
    if not tokens or tokens==['none']:return []
    picked=set()
    for token in tokens:
        if token=='all':picked.update(KEYS)
        elif token in KEYS:picked.add(token)
        elif token.isdigit() and 1<=int(token)<=len(KEYS):picked.add(KEYS[int(token)-1])
        else:raise ValueError(f'Unknown instance group: {token!r}')
    return [k for k in KEYS if k in picked]


# --- WDT and tiles ---
TILE=533.3333333
ORIGIN=17066.6666667

def wdt_path(map_name):
    """Lower-case backslash MPQ name of a map's WDT."""
    return f'world\\maps\\{map_name}\\{map_name}.wdt'.lower()

def tile_of(world_x,world_y):
    """The (x, y) tile name a game-world position falls in (the runtime lookup, world_renderer.h): the axes swap."""
    return math.floor((ORIGIN-world_y)/TILE),math.floor((ORIGIN-world_x)/TILE)

def _chunks(blob):
    offset=0
    while offset+8<=len(blob):
        tag,size=struct.unpack_from('<4sI',blob,offset)
        if offset+8+size>len(blob):raise ValueError('WDT chunk exceeds file: '+repr(tag))
        yield tag[::-1].decode('ascii','replace'),blob[offset+8:offset+8+size]
        offset+=8+size

def read_wdt(blob):
    """{'global_wmo','tiles','mwmo','modf'} of a WDT: MPHD flag bit 0 (a single WMO, no ADTs), the (x, y) of every
    MAIN entry with the has-ADT flag (entry index = y*64+x, ADT <map>_<x>_<y>.adt), the MWMO name (as stored) and
    the one MODF entry as the ADT struct '<2I12f4H' (None if the map has none)."""
    root=dict(_chunks(blob))
    if 'MVER' not in root or 'MPHD' not in root or 'MAIN' not in root:raise ValueError('WDT lacks MVER, MPHD or MAIN')
    if len(root['MPHD'])<4 or len(root['MAIN'])!=64*64*8:raise ValueError('Invalid WDT MPHD or MAIN size')
    flags,=struct.unpack_from('<I',root['MPHD'])
    main=root['MAIN'];tiles=[(i&63,i>>6) for i in range(4096) if struct.unpack_from('<I',main,i*8)[0]&1]
    mwmo=root['MWMO'].split(b'\0',1)[0].decode('utf8') if root.get('MWMO') else None
    modf=struct.unpack_from('<2I12f4H',root['MODF']) if len(root.get('MODF',b''))>=64 else None
    return {'global_wmo':bool(flags&1),'tiles':sorted(tiles),'mwmo':mwmo,'modf':modf}
