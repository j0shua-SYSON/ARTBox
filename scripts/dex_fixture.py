"""Original minimal DEX data fixture; no interpreter or runtime implementation."""
import hashlib
import struct
import zlib

def uleb(value):
    result=bytearray()
    while True:
        byte=value&127;value>>=7
        result.append(byte|(128 if value else 0))
        if not value: return bytes(result)

def make_hello():
    text='hello from ARTBox ART'
    strings=sorted([text,'L','Lartbox/Hello;','Ljava/lang/Object;','Ljava/lang/String;','message'])
    si={s:i for i,s in enumerate(strings)}
    descriptors=['Lartbox/Hello;','Ljava/lang/Object;','Ljava/lang/String;']
    descriptors.sort(key=si.get)
    ti={s:i for i,s in enumerate(descriptors)}
    string_ids=112;type_ids=string_ids+4*len(strings);proto_ids=type_ids+4*len(descriptors)
    method_ids=proto_ids+12;class_defs=method_ids+8;data_off=class_defs+32
    data=bytearray(data_off)
    strings_off=len(data)
    for i,s in enumerate(strings):
        struct.pack_into('<I',data,string_ids+4*i,len(data))
        data+=uleb(len(s))+s.encode('ascii')+b'\0'
    for i,s in enumerate(descriptors): struct.pack_into('<I',data,type_ids+4*i,si[s])
    struct.pack_into('<III',data,proto_ids,si['L'],ti['Ljava/lang/String;'],0)
    struct.pack_into('<HHI',data,method_ids,ti['Lartbox/Hello;'],0,si['message'])
    data+=b'\0'*((-len(data))%4)
    code_off=len(data)
    data+=struct.pack('<4HII3H',1,0,0,0,0,3,0x001a,si[text],0x0011)
    class_data_off=len(data)
    data+=b'\0\0\1\0'+uleb(0)+uleb(9)+uleb(code_off)
    struct.pack_into('<8I',data,class_defs,ti['Lartbox/Hello;'],1,ti['Ljava/lang/Object;'],0,0xffffffff,0,class_data_off,0)
    data+=b'\0'*((-len(data))%4)
    map_off=len(data)
    sections=[(0,1,0),(1,len(strings),string_ids),(2,len(descriptors),type_ids),(3,1,proto_ids),
              (5,1,method_ids),(6,1,class_defs),(0x2002,len(strings),strings_off),
              (0x2001,1,code_off),(0x2000,1,class_data_off),(0x1000,1,map_off)]
    data+=struct.pack('<I',len(sections))
    for kind,count,offset in sections: data+=struct.pack('<HHII',kind,0,count,offset)
    data[:8]=b'dex\n035\0'
    struct.pack_into('<20I',data,32,len(data),112,0x12345678,0,0,map_off,len(strings),string_ids,len(descriptors),type_ids,
                     1,proto_ids,0,0,1,method_ids,1,class_defs,len(data)-data_off,data_off)
    data[12:32]=hashlib.sha1(data[32:]).digest()
    struct.pack_into('<I',data,8,zlib.adler32(data[12:])&0xffffffff)
    return bytes(data),{'class':'Lartbox/Hello;','method':'message','signature':'()Ljava/lang/String;',
                         'expected':text,'code_offset':code_off,'instructions':[0x001a,si[text],0x0011],
                         'class_data_offset':class_data_off,'map_offset':map_off,
                         'method_ids_offset':method_ids,'string_count':len(strings)}


def malformed_inputs(original, layout):
    """Malformed metadata/checksums for the independent AOSP verifier."""
    results = {}
    for name, offset, value in (
        ('duplicate-map-kind', layout['map_offset'] + 4 + 12, 0),
        ('method-name-out-of-range', layout['method_ids_offset'] + 4, layout['string_count']),
        ('code-size-overflow', layout['code_offset'] + 12, 0xffffffff),
    ):
        data = bytearray(original)
        struct.pack_into('<I', data, offset, value)
        data[12:32] = hashlib.sha1(data[32:]).digest()
        struct.pack_into('<I', data, 8, zlib.adler32(data[12:]) & 0xffffffff)
        results[name] = bytes(data)
    data = bytearray(original)
    data[8] ^= 1
    results['bad-checksum'] = bytes(data)
    results['truncated-map'] = original[:-1]
    return results
