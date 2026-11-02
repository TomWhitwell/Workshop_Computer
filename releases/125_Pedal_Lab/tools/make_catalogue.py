#!/usr/bin/env python3
"""Generate browsable local catalogue, credits and reproducible six-slot banks."""
import collections,html,json,pathlib
import catalogue
ROOT=catalogue.ROOT

def main():
    data=catalogue.load();entries=data['programs'];by={e['id']:e for e in entries}
    curated={
      'classic-reverbs':('Keith Barr ROM reverbs and modulation/reverb combinations.', ['directory-publisher-'+s for s in ['rom-rev1','rom-rev2','rom-chor-rev','rom-fla-rev','rom-trem-rev','rev-rt-d-f']]),
      'drolo':('David Rolo: six modulation, pitch and random-delay experiments.', ['directory-drolo-'+s for s in ['envelope-phaser','filter-env-lp-hp-up','flanger','pitch-step-glider','random-loop-delay','ring-pitchulator']]),
      'plates-and-shimmer':('Dattorro plate, Harmon Grold plate, Freeverb and shimmer variants.', ['directory-'+s for s in ['dattorro','rev-pl-1','freeverb','shimmer-1','shimmer-dralx','holy-city-audio-shimmer']]),
      'audiofab-reverbs':('Audiofab: hall, room, plate, spring, shimmer and cascaded reverb; spring has a non-commercial source notice.', ['audiofab-reverb-'+s for s in ['hall-reverb','room-reverb','plate-reverb','spring-reverb','shimmer-reverb','cascaded-reverb']]),
      'otherworld':('Andreas Jansson: experimental granular, spectral and texture algorithms, including LOG/EXP-sensitive diagnostics.', ['otherworld-'+s for s in ['01-prism-scatter-clk','202-grain-bloom','165-strata','132-phase-ghost','179-numbers-station','258-two-whales']]),
    }
    banks={}
    for name,(description,ids) in curated.items():
        assert all(i in by and by[i]['assembly']['status']=='assembled' for i in ids),name
        banks[name]={'description':description,'programs':ids,'kind':'curated'}
    # Numbered pages cover every distinct assemblable bytecode, not repeated
    # website copies. Keep each collection together, then group by main category.
    groups=collections.defaultdict(list)
    for e in entries:
        if e['assembly']['status']=='assembled' and not e.get('duplicate_of'):groups[(e['collection'],e['categories'][0].lower())].append(e)
    n=0
    for (collection,category),rows in sorted(groups.items()):
        for start in range(0,len(rows),6):
            n+=1;ids=[e['id'] for e in rows[start:start+6]]
            banks[f'page-{n:02}']={'description':f'{collection}: {category} ({start+1}–{min(start+6,len(rows))})','programs':ids,'kind':'catalogue-page'}
    data['banks']=banks
    (ROOT/'algorithms/catalogue.json').write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n')
    (ROOT/'algorithms/README.md').write_text('# Algorithm library\n\nAuthor credits, controls, source links and reuse terms are in `catalogue.json` and the editor. Original sources and notices are kept in `upstream/`; `ready/` contains the copies used for building.\n')
    template=(ROOT/'tools/catalogue.html').read_text()
    (ROOT/'algorithms/index.html').write_text(template.replace('/*CATALOGUE_DATA*/',json.dumps(data,ensure_ascii=True).replace('<','\\u003c')))
    print(f'{len(entries)} entries; {len(banks)} banks ({n} catalogue pages)')
if __name__=='__main__':main()
