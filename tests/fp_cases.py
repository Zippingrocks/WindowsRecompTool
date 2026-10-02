"""Author-written, x87-only instruction sequences; memory uses ESI/EDI + displacement.

They are executable in both IA-32 and long mode without changing FP semantics.
The native reference sets RSI/RDI to host buffers and runs each original sequence
as one uninterrupted block. WinRecomp separately decodes/lifts it, using checked
guest memory and an explicit 80-bit state transfer per instruction.
"""
def cases():
    out=[]
    def add(name,code,fmt='f32',environment=False):out.append({'name':name,'code':code+'c3','format':fmt,'environment':environment})
    add('load32','d906');add('load64','dd06','f64');add('load80','db2e','ext80')
    for name,code,fmt in [('i16','df06','i16'),('i32','db06','i32'),('i64','df2e','i64')]:add('load_'+name,code,fmt)
    for name,code in [('store32','d91f'),('store64','dd1f'),('store80','db3f'),('store_i16','df1f'),('store_i32','db1f'),('store_i64','df3f')]:add(name,'d906'+code)
    for name,op in [('add','46'),('mul','4e'),('com','56'),('comp','5e'),('sub','66'),('subr','6e'),('div','76'),('divr','7e')]:
        add(name+'_mem32','d906d8'+op+'04')
        add(name+'_mem64','dd06dc'+op+'08','f64')
    for name,op in [('add','c1'),('mul','c9'),('com','d1'),('comp','d9'),('sub','e1'),('subr','e9'),('div','f1'),('divr','f9')]:add(name+'_reg','d906d94604d8'+op)
    for name,op in [('addp','c1'),('mulp','c9'),('subrp','e1'),('subp','e9'),('divrp','f1'),('divp','f9')]:add(name,'d906d94604de'+op)
    for name,op in [('abs','e1'),('chs','e0'),('sqrt','fa'),('sin','fe'),('cos','ff'),('sincos','fb'),('round','fc'),('examine','e5'),('test','e4'),('2xm1','f0'),('tan','f2'),('extract','f4')]:add(name,'d906d9'+op)
    for name,op in [('atan','f3'),('scale','fd'),('yl2x','f1'),('yl2xp1','f9'),('remainder','f8'),('remainder1','f5')]:add(name,'d906d94604d9'+op)
    for name,op in [('one','e8'),('l2t','e9'),('l2e','ea'),('pi','eb'),('lg2','ec'),('ln2','ed'),('zero','ee')]:add('constant_'+name,'d9'+op)
    add('exchange','d906d94604d9c9');add('compp','d906d94604ded9');add('ucompp','d906d94604dae9')
    add('comi','d906d94604dbf1');add('ucomi','d906d94604dbe9');add('comip','d906d94604dff1');add('ucomip','d906d94604dfe9')
    for op in ['dac1','dac9','dad1','dad9','dbc1','dbc9','dbd1','dbd9']:add('cmov_'+op,'d906d94604'+op)
    add('free','d906ddc0');add('dec_top','d906d9f6');add('inc_top','d906d9f7')
    add('status_ax','d906d94604ded9dfe0');add('control_store','d93f');add('status_store','dd3f')
    add('clear','d906d9fadde2'.replace('dde2','dbe2'))
    add('wait','d9069b');add('stack_overflow','d9e8'*9);add('stack_underflow','d8c1')
    add('environment','d906d94604d937','f32',True)
    add('save_restore','d906d94604dd37dd27','f32',True)
    add('environment_top_rotation','d9e8d9eed93766814f040008d927dd5f20','f32',True)
    out[-1]['flag_mask']=0x8c5  # OR leaves AF undefined.
    return out
