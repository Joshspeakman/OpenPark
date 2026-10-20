// Package actual native captures for review without a platform video encoder.
// The paired, unquantized PNGs remain the source of truth for pixel comparison.
import Foundation
import CoreGraphics
import CoreText
import ImageIO
import UniformTypeIdentifiers
let args=CommandLine.arguments
if args.count != 5 { fatalError("usage: capture_gif directory output.gif count fps") }
let dir=URL(fileURLWithPath:args[1]),out=URL(fileURLWithPath:args[2])
let count=Int(args[3])!,fps=Double(args[4])!
precondition(count>0 && count<=2000 && fps>0 && fps<=30)
let width=1280,height=438
let dest=CGImageDestinationCreateWithURL(out as CFURL,UTType.gif.identifier as CFString,count,nil)!
CGImageDestinationSetProperties(dest,[kCGImagePropertyGIFDictionary:[kCGImagePropertyGIFLoopCount:0]] as CFDictionary)
for i in 0..<count {
 try autoreleasepool {
  let ctx=CGContext(data:nil,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
  ctx.setFillColor(CGColor(red:0.08,green:0.11,blue:0.15,alpha:1));ctx.fill(CGRect(x:0,y:0,width:width,height:height));ctx.interpolationQuality = .none
  for side in 0...1 {
   let name=String(format:"live-%03d%@.png",i,side==1 ? "-original":"")
   guard let src=CGImageSourceCreateWithURL(dir.appendingPathComponent(name) as CFURL,nil),let image=CGImageSourceCreateImageAtIndex(src,0,nil) else {throw NSError(domain:"Missing frame",code:i)}
   ctx.draw(image,in:CGRect(x:side*640,y:0,width:640,height:400))
   let label=side==0 ? "REIMAGINED · guest animation prototype" : "ORIGINAL ART · same native simulation frame"
   let attrs:[NSAttributedString.Key:Any]=[NSAttributedString.Key(kCTFontAttributeName as String):CTFontCreateWithName("Helvetica" as CFString,15,nil),NSAttributedString.Key(kCTForegroundColorAttributeName as String):CGColor(gray:1,alpha:1)]
   let line=CTLineCreateWithAttributedString(NSAttributedString(string:label,attributes:attrs));ctx.textPosition=CGPoint(x:side*640+12,y:height-24);CTLineDraw(line,ctx)
  }
  CGImageDestinationAddImage(dest,ctx.makeImage()!,[kCGImagePropertyGIFDictionary:[kCGImagePropertyGIFDelayTime:1/fps,kCGImagePropertyGIFUnclampedDelayTime:1/fps]] as CFDictionary)
 }
}
precondition(CGImageDestinationFinalize(dest));print("Encoded \(count) paired frames: \(out.path)")
