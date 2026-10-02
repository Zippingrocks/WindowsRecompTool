"""Author-written SSE/SSE2 fixtures, valid unchanged in IA-32 and long mode."""
def cases():
    out=[]
    def add(name,code,fmt='simd32'):out.append({'name':name,'code':code+'c3','format':fmt,'environment':False})
    for prefix,suffix,fmt in [('', 'ps','simd32'),('66','pd','simd64'),('f3','ss','simd32'),('f2','sd','simd64')]:
        for name,opcode in [('add','58'),('mul','59'),('sub','5c'),('min','5d'),('div','5e'),('max','5f')]:
            add(name+suffix+'_reg',prefix+'0f1006'+prefix+'0f104e10'+prefix+'0f'+opcode+'c1',fmt)
            add(name+suffix+'_memory',prefix+'0f1006'+prefix+'0f'+opcode+'4610',fmt)
        add('sqrt'+suffix,prefix+'0f5106',fmt)
        add('mov'+suffix+'_load_store',prefix+'0f1006'+prefix+'0f1107',fmt)
        add('mov'+suffix+'_register',prefix+'0f1006'+prefix+'0f104e10'+prefix+'0f10c1',fmt)
    for prefix,suffix,fmt in [('', 'ps','simd32'),('66','pd','simd64')]:
        for name,op in [('and','54'),('andn','55'),('or','56'),('xor','57')]:add(name+suffix,prefix+'0f1006'+prefix+'0f'+op+'4610',fmt)
        for name,op in [('ucomi','2e'),('comi','2f')]:add(name+suffix,prefix+'0f1006'+prefix+'0f'+op+'4610',fmt)
        add('aligned_move'+suffix,prefix+'0f2806'+prefix+'0f2907',fmt)
    for prefix,suffix in [('', 'ps'),('f3','ss')]:
        add('rcp'+suffix,prefix+'0f5306');add('rsqrt'+suffix,prefix+'0f5206')
    for prefix,opcode,name,fmt in [('', '5a','cvtps2pd','simd32'),('66','5a','cvtpd2ps','simd64'),('f3','5a','cvtss2sd','simd32'),('f2','5a','cvtsd2ss','simd64'),('', '5b','cvtdq2ps','simd_integer'),('66','5b','cvtps2dq','simd32'),('f3','5b','cvttps2dq','simd32'),('66','e6','cvttpd2dq','simd64'),('f2','e6','cvtpd2dq','simd64'),('f3','e6','cvtdq2pd','simd_integer')]:
        add(name,prefix+'0f'+opcode+'06',fmt)
    for opcode in ['60','61','62','63','64','65','66','67','68','69','6a','6b','6c','6d','74','75','76','d1','d2','d3','d4','d5','d8','d9','da','db','dc','dd','de','df','e0','e1','e2','e3','e4','e5','e8','e9','ea','eb','ec','ed','ee','ef','f1','f2','f3','f4','f5','f6','f8','f9','fa','fb','fc','fd','fe']:
        add('packed_integer_'+opcode,'660f6f06'+'660f'+opcode+'4610','simd_integer')
    add('movdqa','660f6f06660f7f07','simd_integer');add('movdqu','f30f6f06f30f7f07','simd_integer')
    for n in range(8):add('xmm_register_'+str(n),'0f10'+f'{0xc0+n*8+((n+1)%8):02x}'+'0f58'+f'{0xc0+n*8+((n+2)%8):02x}')
    add('mxcsr_load_store','0fae160fae1f','mxcsr')
    return out
