"""The goal compiler C(g; p0) = Link(p0, Encode(g), E(g)).

Encode(g) (`common`) is the fixed translation of a task into constant stage tables,
progress tracking and the hard resource-reserve filter. E(g) is an emitter: a
Python module whose emit(goal) returns helper definitions and one controller rule.
Link (`link`) inserts that rule into p0 above r_strategic, i.e. below the
inherited survival rules, and wires the progress hook and the reserve filter.

    python3 compiler/compiler.py GOAL.json --arm synthesized --controller EMITTER.py --output compiler/build/NAME
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re

from paths import HERE,BASE_PROGRAM

RESOURCES=('wood','stone','coal','iron','diamond')
ARMS=('original','guards','reference','synthesized')
SPENDING={
 'WOOD_PICK':(1,0,0,0,0),'WOOD_SWORD':(1,0,0,0,0),
 'STONE_PICK':(1,1,0,0,0),'STONE_SWORD':(1,1,0,0,0),
 'IRON_PICK':(1,1,1,1,0),'IRON_SWORD':(1,1,1,1,0),
 'DIAMOND_PICK':(1,0,0,0,3),'DIAMOND_SWORD':(1,0,0,0,2),
 'IRON_ARMOUR':(0,0,3,3,0),'DIAMOND_ARMOUR':(0,0,0,0,3),
 'PLACE_TABLE':(2,0,0,0,0),'PLACE_FURNACE':(0,1,0,0,0),
 'PLACE_STONE':(0,1,0,0,0),'MAKE_ARROW':(1,1,0,0,0),'MAKE_TORCH':(1,0,1,0,0)}

def stock(x,label):
    if not isinstance(x,dict) or set(x)-set(RESOURCES):raise ValueError(f'invalid {label}')
    if any(type(v) is not int or not 0<=v<=20 for v in x.values()):raise ValueError(f'{label} bounds must be integers 0..20')
    return {r:x.get(r,0) for r in RESOURCES}

def parse_goal(raw):
    if not isinstance(raw,dict) or set(raw)!={'name','sequence'}:raise ValueError('expected name and sequence')
    if not isinstance(raw['name'],str) or not re.fullmatch('[a-z][a-z0-9_]{0,63}',raw['name']):raise ValueError('invalid name')
    seq=raw['sequence']
    if not isinstance(seq,list) or not 1<=len(seq)<=8:raise ValueError('sequence needs 1..8 stages')
    result=[]
    for s in seq:
        if not isinstance(s,dict):raise ValueError('invalid stage')
        if set(s)=={'reach'}:
            if type(s['reach']) is not int or not 0<=s['reach']<=8:raise ValueError('reach floor must be 0..8')
            result.append({'reach':s['reach']})
        elif 'acquire' in s and not set(s)-{'acquire','maintain'}:
            need=stock(s['acquire'],'acquire');keep=stock(s.get('maintain',{}),'maintain')
            if not any(need.values()) or any(keep[r]>need[r] for r in RESOURCES):raise ValueError('maintain must be bounded by a nonempty acquire')
            result.append({'acquire':need,'maintain':keep})
        else:raise ValueError('stage must be reach or acquire with optional maintain')
    return {'name':raw['name'],'sequence':result}

def vectors(goal):
    return [(s.get('reach',-1),[s.get('acquire',{}).get(r,0) for r in RESOURCES],
             [s.get('maintain',{}).get(r,0) for r in RESOURCES]) for s in goal['sequence']]

def common(goal):
    v=vectors(goal)
    out=[f'const SQ_N={len(v)};', 'const int sq_floor[]={'+','.join(str(s[0]) for s in v)+'};',
         'const int sq_stock[]={'+','.join(str(x) for s in v for x in s[1])+'};',
         'const int sq_hold[]={'+','.join(str(x) for s in v for x in s[2])+'};',r'''
def int sq_done(){return a.intent.stage>=SQ_N;}
def int sq_target(){return sq_done()?-1:sq_floor[a.intent.stage];}
def int sq_need(int r){return sq_done()?0:sq_stock[a.intent.stage*5+r];}
def int sq_reserve(int r){int n=0;for(int j=0;j<a.intent.stage && j<SQ_N;j++)if(sq_hold[j*5+r]>n)n=sq_hold[j*5+r];return n;}
def void sq_observe(){
 if(sq_done() || o.health<=0)return;
 int k=a.intent.stage;
 if(sq_floor[k]>=0 && sq_floor[k]!=o.level)return;
 for(int r=0;r<5;r++)if(o.inv[r]<sq_stock[k*5+r])return;
 a.intent.stage++;
}
def int sq_forbidden(int action){
''']
    for action,costs in SPENDING.items():
        # Zero reserves do not forbid spending an unavailable input: failed
        # engine actions consume nothing. Only active positive bounds matter.
        terms=[f'(sq_reserve({r})>0 && o.inv[{r}]-{n}<sq_reserve({r}))' for r,n in enumerate(costs) if n]
        out.append(f" if(action=={action} && ({' || '.join(terms)}))return 1;")
    out+=[' return 0;\n}',r'''
def int filter(int action,out int skill){
 action=sq_base_filter(action,&skill);
 if(sq_forbidden(action)){skill=RECOVER;return NOOP;}
 return action;
}
''']
    return '\n'.join(out)

def emitter(path):
    spec=importlib.util.spec_from_file_location('sequence_emitter',path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    return mod

def link(goal,dest,arm,controller=None):
    goal=parse_goal(goal);dest=Path(dest).resolve()
    if arm not in ARMS:raise ValueError('invalid arm')
    if not dest.is_relative_to(HERE/'build') or dest.exists():
        raise ValueError('output must be a new directory under compiler/build')
    control=None;emitted=None
    if arm=='synthesized' and controller is None:raise ValueError('the synthesized arm needs an emitter (--controller)')
    if arm in ('reference','synthesized'):
        control=Path(controller or HERE/f'{arm}.py').resolve()
        if control.suffix!='.py':raise ValueError('controller must be Python source')
        emitted=emitter(control).emit(goal)
        if set(emitted)!={'defs','rule','anchor'}:raise ValueError('bad emitter API')
    texts={p.name:p.read_text() for p in sorted(BASE_PROGRAM.glob('*.tr'))}
    if arm!='original':
        texts['filter.tr'],n=re.subn(r'def int filter\(', 'def int sq_base_filter(',texts['filter.tr']);assert n==1
        hook='def void observe(int changed) {';assert texts['hooks.tr'].count(hook)==1
        texts['hooks.tr']=texts['hooks.tr'].replace(hook,hook+'\n sq_observe();')
        texts['sequence.tr']=common(goal)
    if emitted:
        anchor=emitted['anchor']
        if texts['floor8.tr'].count(anchor)!=1:raise ValueError('ambiguous insertion anchor')
        texts['floor8.tr']=texts['floor8.tr'].replace(anchor,emitted['rule']+'\n\n'+anchor)
        texts['controller.tr']=emitted['defs']
    dest.mkdir(parents=True)
    for n,t in texts.items():(dest/n).write_text(t)
    names=['floor8.tr']+sorted(n for n in texts if n!='floor8.tr')
    (dest/'ORDER').write_text('\n'.join(names)+'\n');(dest/'goal.json').write_text(json.dumps(goal,indent=2)+'\n')
    plan={'goal':goal,'arm':arm,'controller':os.path.relpath(control,HERE) if control else None}
    (dest/'plan.json').write_text(json.dumps(plan,indent=2)+'\n');return dest

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('goal',type=Path);ap.add_argument('--arm',choices=ARMS,required=True)
    ap.add_argument('--output',type=Path,required=True);ap.add_argument('--controller',type=Path);args=ap.parse_args()
    try:print(link(json.loads(args.goal.read_text()),args.output,args.arm,args.controller))
    except ValueError as exc:ap.error(str(exc))
