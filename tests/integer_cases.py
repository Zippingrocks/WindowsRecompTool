"""Author-created byte fixtures for integer, prefix and indirect-flow semantics."""
def cases():
    out={}
    for name,extension in [('shl',4),('shr',5),('sar',7),('rol',0),('ror',1),('rcl',2),('rcr',3)]:
        for width,prefix,opcode in [(8,'','d2'),(16,'66','d3'),(32,'','d3')]:
            out[f'{name}_{width}_cl']=prefix+opcode+f'{0xc0+extension*8:02x}'+'c3'
            out[f'{name}_{width}_one']=prefix+('d0' if width==8 else 'd1')+f'{0xc0+extension*8:02x}'+'c3'
    for width,prefix,opcode in [(8,'','f6'),(16,'66','f7'),(32,'','f7')]:
        out[f'mul{width}']=prefix+opcode+'e3c3'
        out[f'imul{width}_one']=prefix+opcode+'ebc3'
    for width,prefix in [(16,'66'),(32,'')]:
        out[f'imul{width}_two']=prefix+'0fafc3c3'
        out[f'imul{width}_three']=prefix+'6bc37fc3'
        out[f'imul{width}_negative']=prefix+'6bc3ffc3'
        out[f'shld{width}']=('83e10f' if width==16 else '')+prefix+'0fa5d8c3'
        out[f'shrd{width}']=('83e10f' if width==16 else '')+prefix+'0fadd8c3'
        out[f'shld{width}_full']=prefix+'0fa4d8'+f'{width:02x}'+'c3'
        out[f'shrd{width}_full']=prefix+'0facd8'+f'{width:02x}'+'c3'
        out[f'bsf{width}']=prefix+'0fbcc3c3'
        out[f'bsr{width}']=prefix+'0fbdc3c3'
        for n,op in [('bt','a3'),('bts','ab'),('btr','b3'),('btc','bb')]:
            out[f'{n}{width}']=prefix+'0f'+op+'d8c3'
    out.update({
      'div8':'25000000000fb6c383c801b303f6f3c3',
      'div16':'6631d26683cb0166f7f3c3',
      'div32':'31d283cb01f7f3c3',
      'idiv8':'6698b303f6fbc3',
      'idiv16':'669966bbfdff66f7fbc3',
      'idiv32':'99bbfdfffffff7fbc3',
      'cbw_cwd':'66986699c3','cwde_cdq':'9899c3',
      'xchg':'93c3','xchg8':'86e3c3','xchg16':'6687d8c3',
      'xchg_memory':'87442404c3','xchg_address_alias':'8d4424048700c3',
      'xadd':'0fc1d8c3','xadd_alias':'0fc1c0c3','xadd_memory':'0fc15c2404c3',
      'cmpxchg':'0fb1d9c3','cmpxchg_memory':'0fb15c2404c3',
      'cmpxchg_equal':'89c10fb1d9c3',
      'lock_add':'f001442404c3','lock_xadd':'f00fc15c2404c3','lock_cmpxchg':'f00fb15c2404c3',
      'bt_signed_memory':'83e17f83e9400fa30ec3',
      'bts_signed_memory':'83e17f83e9400fab0ec3',
      'btr_signed_memory':'83e17f83e9400fb30ec3',
      'btc_signed_memory':'83e17f83e9400fbb0ec3',
      'bswap':'0fc8c3','lahf_sahf':'9f9ec3',
      'pop_memory_esp':'50668f0424c3',
      'loop':'b90500000001d8e2fcc3',
      'loope':'b90500000031c0e1fcc3',
      'loopne':'b90500000040e0fdc3',
      'loop16':'b90512345601d8664983e100c3',
      'indirect_importstyle':'8d4424048b00ffe0', # ret through existing STOP stack word.
    })
    # This fixture pops a full word on a temporary stack; restore before RET.
    out['pop_memory_esp']='508f0424c3'
    # Popping EAX into [new ESP] overwrites the return address, so use a frame.
    out['pop_memory_esp']='83ec08508f042483c408c3'
    # Tail transfer to the sentinel is supported without guessing a function.
    out['indirect_importstyle']='8b042483c404ffe0'
    out['loop16']='b90500345601d867e2fbc3'
    for cc in range(16):
        out[f'setcc_{cc:x}']='0f'+f'{0x90+cc:02x}'+'c0c3'
        out[f'cmovcc_{cc:x}']='0f'+f'{0x40+cc:02x}'+'c3c3'
    for name,op in [('movsb','a4'),('movsw','66a5'),('movsd','a5'),('stosb','aa'),('stosw','66ab'),('stosd','ab'),('lodsb','ac'),('lodsw','66ad'),('lodsd','ad')]:
        out['rep_'+name]='83e11ff3'+op+'c3'
        out['rep_'+name+'_reverse']='83e11ffdf3'+op+'fcc3'
    for name,op in [('cmpsb','a6'),('cmpsw','66a7'),('cmpsd','a7'),('scasb','ae'),('scasw','66af'),('scasd','af')]:
        out['repe_'+name]='83e11ff3'+op+'c3'
        out['repne_'+name]='83e11ff2'+op+'c3'
    out.update({
      'pushfd_read':'9c58c3',
      'pushf16_read':'669c6658c3',
      'popfd_roundtrip':'9c9dc3',
      'popf16_roundtrip':'669c669dc3',
      'popfd_id_toggle':'9c5889c23500002000509d9c5831d0c3',
      'popfd_status_df':'68070e00009d9c58fcc3',
      'popf16_status_df':'6668070e669d669c6658fcc3',
    })
    return out
