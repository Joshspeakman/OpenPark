// Decode a generated texture into a portable RGBA input for the model compiler.
// No repainting, background removal or asset generation takes place here.
import Foundation
import ImageIO
import CoreGraphics
let args=CommandLine.arguments
guard args.count==3 else { fatalError("usage: remaster_texture INPUT.png OUTPUT.rgba") }
guard let src=CGImageSourceCreateWithURL(URL(fileURLWithPath:args[1]) as CFURL,nil),
      let image=CGImageSourceCreateImageAtIndex(src,0,nil) else { fatalError("Invalid image") }
let w=image.width,h=image.height
guard w>0,h>0,w<=4096,h<=4096 else { fatalError("Texture dimensions outside limits") }
var bytes=[UInt8](repeating:0,count:w*h*4)
let cs=CGColorSpaceCreateDeviceRGB()
guard let ctx=CGContext(data:&bytes,width:w,height:h,bitsPerComponent:8,bytesPerRow:w*4,space:cs,
                       bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)
else { fatalError("Cannot create texture decoder") }
// A bitmap context writes the decoded image's first row at byte zero.
// Flipping the CTM here would invert the atlas relative to its UV coordinates.
ctx.draw(image,in:CGRect(x:0,y:0,width:w,height:h))
// Convert to straight alpha; native rig textures store RRGGBBAA.
for i in stride(from:0,to:bytes.count,by:4) {
    let a=Int(bytes[i+3])
    if a>0 && a<255 {
        for k in 0..<3 {bytes[i+k]=UInt8(min(255,(Int(bytes[i+k])*255+a/2)/a))}
    }
}
var data=Data("TPRGBA01".utf8)
for value in [UInt32(w),UInt32(h)] {
    var v=value.littleEndian;withUnsafeBytes(of:&v) {data.append(contentsOf:$0)}
}
data.append(contentsOf:bytes)
try data.write(to:URL(fileURLWithPath:args[2]),options:.atomic)
print("\(w)x\(h) RGBA -> \(args[2])")
