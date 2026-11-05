"""Actions-only runner; grade.sh alone returns zero even after mismatches."""
from pathlib import Path
import subprocess,json
root=Path(__file__).resolve().parents[1]
subprocess.run(['cmake','-S',str(root),'-B',str(root/'build'),'-DCMAKE_BUILD_TYPE=Release'],check=True)
subprocess.run(['cmake','--build',str(root/'build'),'--target','test_slp','test_slp_side_effects','-j2'],check=True)
results=[]
for number in range(7):
    run=subprocess.run([str(root/'build/test_slp'),str(number)],capture_output=True,text=True,check=True,timeout=10)
    expected=(root/f'testdata/lab1/refs/ref-{number}.txt').read_text()
    assert run.stdout.split()==expected.split(),(number,run.stdout,expected)
    results.append({'test':number,'status':'passed','output':run.stdout})
run=subprocess.run([str(root/'build/test_slp_side_effects')],capture_output=True,text=True,check=True,timeout=10)
assert run.stdout.split()==['1','2','3','4','5','5'],run.stdout
(root/'build/lab1-results.json').write_text(json.dumps({'original_tests':results,'side_effects':'passed','original_score':100,'scope':'public starter tests plus semantic regression, not a private grade'},indent=2)+'\n')
print('Seven unchanged original outputs and side-effect regression passed; public score 100')
