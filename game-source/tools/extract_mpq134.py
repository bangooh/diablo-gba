import struct
def read_mpq(archive,name):
    import dclimplode
    h=archive.get_hash_table_entry(name)
    if h is None: raise FileNotFoundError(name)
    b=archive.block_table[h.block_table_index]
    archive.file.seek(archive.header['offset']+b.offset)
    raw=archive.file.read(b.archived_size)
    key=archive._hash(name.rsplit('\\',1)[-1],'TABLE')
    if b.flags&0x20000:key=((key+b.offset)^b.size)&0xffffffff
    def decrypt(data,k):
        end=len(data)&~3
        return archive._decrypt(data[:end],k&0xffffffff)+data[end:]
    def unpack(data,size):
        if len(data)==size:return data
        if b.flags&0x100:return dclimplode.decompressobj().decompress(data)
        raise ValueError('Unexpected compression flags')
    if not b.flags&0x300:
        # Uncompressed MPQ files have no sector-offset table, even encrypted WAVs.
        sector_size=512<<archive.header['sector_size_shift']
        result=b''.join(decrypt(raw[i:i+sector_size],key+i//sector_size) if b.flags&0x10000 else raw[i:i+sector_size] for i in range(0,b.size,sector_size))
    elif b.flags&0x1000000:
        if b.flags&0x10000:raw=decrypt(raw,key)
        result=unpack(raw,b.size)
    else:
        sector_size=512<<archive.header['sector_size_shift']
        count=(b.size+sector_size-1)//sector_size
        table=raw[:4*(count+1)]
        if b.flags&0x10000:table=decrypt(table,key-1)
        offsets=struct.unpack('<'+'I'*(count+1),table)
        assert offsets[0]==len(table) and offsets[-1]<=len(raw),(name,offsets[:3])
        parts=[]
        for i in range(count):
            data=raw[offsets[i]:offsets[i+1]]
            if b.flags&0x10000:data=decrypt(data,key+i)
            size=min(sector_size,b.size-i*sector_size)
            data=unpack(data,size)
            assert len(data)==size,(name,i,len(data),size)
            parts.append(data)
        result=b''.join(parts)
    assert len(result)==b.size
    return result

