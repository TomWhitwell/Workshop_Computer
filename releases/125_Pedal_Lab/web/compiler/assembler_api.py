import sys,json,struct,pathlib,contextlib,io
sys.path.insert(0,'/app/tools')
import compile_bank,index_catalogue

def assemble_request(payload):
    rows=payload['programs'];paths=[];stats=[];all_words=[]
    folder=pathlib.Path('/app/input');folder.mkdir(exist_ok=True)
    for i,row in enumerate(rows):
        source,changes=index_catalogue.normalize(row['source'])
        path=folder/f'{i}.spn';path.write_text(source);capture=io.StringIO()
        try:
            with contextlib.redirect_stdout(capture):words=compile_bank.assemble(path)
        except (SystemExit,ValueError) as e:raise ValueError(capture.getvalue().strip() or str(e)) from None
        used=128
        while used and words[used-1]==17:used-=1
        all_words.append(words)
        stats.append({'instructions':used,'normalizations':len(changes)})
        paths.append(path)
    if payload['type']=='check':return json.dumps(stats[0])
    output=pathlib.Path('/app/output');output.mkdir(exist_ok=True)
    compile_bank.generate(paths,output)
    header=(output/'bank.hpp').read_text().replace('inline constexpr unsigned program_count','extern const unsigned program_count').replace('inline constexpr Program programs','extern const Program programs')
    return json.dumps({'header':header,'stats':stats,'words':all_words})
