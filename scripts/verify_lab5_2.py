"""Build and run generated virtual assembly on Actions, not on the local host."""
import hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'reports/lab5-2-results'
def run(args,**kwargs):return subprocess.run(args,cwd=ROOT,text=True,capture_output=True,**kwargs)
def normalized(text):return [re.sub(r'[ \t\r\v\f]','',line) for line in text.splitlines() if line.strip()]
def main():
    OUT.mkdir(parents=True,exist_ok=True);records=[]
    try:
        for args in [['cmake','-S','.','-B','build','-DCMAKE_BUILD_TYPE=Release'],['cmake','--build','build','--parallel','2','--target','test_codegen']]:
            p=run(args,timeout=300)
            with (OUT/'build.log').open('a') as f:f.write(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError('Release build failed; inspect build.log')
        cases=OUT/'cases';cases.mkdir(exist_ok=True)
        for source in sorted((ROOT/'testdata/lab5or6/testcases').glob('*.tig')):
            target=cases/source.name;target.write_bytes(source.read_bytes())
            compiled=run([str(ROOT/'build/test_codegen'),str(target)],timeout=30)
            (OUT/f'compile-{source.stem}.txt').write_text(compiled.stdout+compiled.stderr)
            inputs=sorted((source.parent/'merge').glob('*.in')) if source.stem=='merge' else [None]
            for stdin in inputs:
                name=source.stem+('/'+stdin.stem if stdin else '')
                ref=ROOT/'testdata/lab5or6/refs'/('merge/'+stdin.stem+'.out' if stdin else source.stem+'.out')
                if compiled.returncode:
                    records.append({'name':name,'kind':'official_virtual_assembly','passed':False,'stage':'compile','exit':compiled.returncode});continue
                p=run([sys.executable,str(ROOT/'scripts/lab5_test/main.py'),str(target)+'.s'],input=stdin.read_text() if stdin else '',timeout=60)
                (OUT/(name.replace('/','-')+'.txt')).write_text(p.stdout+'\nSTDERR\n'+p.stderr)
                records.append({'name':name,'kind':'official_virtual_assembly','passed':p.returncode==0 and normalized(p.stdout)==normalized(ref.read_text()),'exit':p.returncode,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'reference_sha256':hashlib.sha256(ref.read_bytes()).hexdigest()})
    finally:
        sha=os.getenv('GITHUB_SHA') or run(['git','rev-parse','HEAD']).stdout.strip()
        (OUT/'results.json').write_text(json.dumps({'commit':sha,'scope':'Lab5-2 generated virtual-register assembly on unmodified provided interpreter; no native register-allocation claim','tests':records,'passed':sum(x['passed'] for x in records),'total':len(records)},indent=2)+'\n')
    failed=[x['name'] for x in records if not x['passed']]
    print(f'{len(records)-len(failed)}/{len(records)} passed')
    if failed:raise SystemExit('Failed: '+', '.join(failed))
if __name__=='__main__':main()
