"use client";

import { useState, useEffect } from "react";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Slider } from "@/components/ui/slider";
import { Pause, Play, Rss, UploadCloud, Zap, ZapOff, Cog, RefreshCw } from "lucide-react";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog";

type HardwareControlsProps = {
  ipAddress: string;
  setIpAddress: (ip: string) => void;
  isConnected: boolean;
  handleConnect: () => void;
  sendCommand: (command: object) => boolean;
  isUploading: boolean;
  handleUploadSequence: () => void;
};

export function HardwareControls({
  ipAddress,
  setIpAddress,
  isConnected,
  handleConnect,
  sendCommand,
  isUploading,
  handleUploadSequence
}: HardwareControlsProps) {
  const [speed, setSpeed] = useState(100);
  const [color, setColor] = useState("#7DF9FF");
  const [isPlaying, setIsPlaying] = useState(false);
  const [isDetectingIP, setIsDetectingIP] = useState(false);

  useEffect(() => {
    // Sync initial color on connect
    if (isConnected) {
        sendCommand({ action: "color", value: color });
    } else {
        setIsPlaying(false);
    }
  }, [isConnected]);

  const handleSpeedChange = (value: number[]) => {
    const newSpeed = 520 - value[0];
    setSpeed(newSpeed);
    sendCommand({ action: "speed", value: newSpeed });
  };
  
  const handleColorChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setColor(e.target.value);
    sendCommand({ action: "color", value: e.target.value });
  };

  const handlePlayPause = () => {
    const newIsPlaying = !isPlaying;
    if (sendCommand({ action: newIsPlaying ? "play" : "pause" })) {
        setIsPlaying(newIsPlaying);
    }
  };

  const handleRebootToAP = () => {
    if(sendCommand({ action: "reboot_to_ap" })) {
      // The ESP32 will reboot, which will close the connection.
      // The onclose event handler in page.tsx will handle state changes.
    }
  };

  const detectESP32IP = async () => {
    setIsDetectingIP(true);
    
    // Common ESP32 IP ranges to scan
    const commonIPs = [
      "192.168.4.1",    // AP mode default
      "192.168.1.100",  // Common router range
      "192.168.1.101",
      "192.168.1.102",
      "192.168.0.100",  // Another common range
      "192.168.0.101",
      "192.168.0.102",
      "10.0.0.100",     // Some routers use this
      "10.0.0.101"
    ];

    // Get current network range from browser if possible
    try {
      // Try to detect current network range
      const response = await fetch('/api/ip').catch(() => null);
      if (response && response.ok) {
        const data = await response.json();
        if (data.ip) {
          commonIPs.unshift(data.ip);
        }
      }
    } catch (e) {
      // Ignore errors, continue with common IPs
    }

    // Test each IP
    for (const testIP of commonIPs) {
      try {
        const response = await fetch(`http://${testIP}/api/ip`, {
          method: 'GET',
          timeout: 2000,
          signal: AbortSignal.timeout(2000)
        });
        
        if (response.ok) {
          const data = await response.json();
          if (data.ip) {
            setIpAddress(data.ip);
            setIsDetectingIP(false);
            return;
          }
        }
      } catch (e) {
        // Continue to next IP
        continue;
      }
    }
    
    // If no IP found, try WebSocket scan
    for (const testIP of commonIPs) {
      try {
        const ws = new WebSocket(`ws://${testIP}/ws`);
        
        await new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            ws.close();
            reject(new Error('Timeout'));
          }, 1000);
          
          ws.onopen = () => {
            clearTimeout(timeout);
            ws.close();
            setIpAddress(testIP);
            resolve(testIP);
          };
          
          ws.onerror = () => {
            clearTimeout(timeout);
            reject(new Error('Connection failed'));
          };
        });
        
        setIsDetectingIP(false);
        return;
      } catch (e) {
        // Continue to next IP
        continue;
      }
    }
    
    setIsDetectingIP(false);
    alert('Could not automatically detect ESP32. Please enter the IP address manually.');
  };

  return (
    <Card className="bg-card border-border shadow-2xl shadow-black/25">
      <CardHeader>
        <CardTitle className="flex items-center justify-between text-primary font-headline">
          <div className="flex items-center gap-2">
            <Rss className="h-6 w-6" />
            Hardware Controls
          </div>
          {isConnected && (
             <AlertDialog>
              <AlertDialogTrigger asChild>
                <Button variant="ghost" size="icon" className="h-8 w-8 text-muted-foreground hover:text-primary">
                  <Cog className="h-5 w-5" />
                </Button>
              </AlertDialogTrigger>
              <AlertDialogContent>
                <AlertDialogHeader>
                  <AlertDialogTitle>Reconfigure Device?</AlertDialogTitle>
                  <AlertDialogDescription>
                    This will reboot the device into Access Point (AP) mode. You will be disconnected and will need to connect to the "DigitalWaterCurtain-Setup" WiFi network to reconfigure it. Are you sure?
                  </AlertDialogDescription>
                </AlertDialogHeader>
                <AlertDialogFooter>
                  <AlertDialogCancel>Cancel</AlertDialogCancel>
                  <AlertDialogAction onClick={handleRebootToAP}>Reboot to AP Mode</AlertDialogAction>
                </AlertDialogFooter>
              </AlertDialogContent>
            </AlertDialog>
          )}
        </CardTitle>
        <CardDescription>Connect to and control your physical water curtain.</CardDescription>
      </CardHeader>
      <CardContent className="space-y-6">
        <div className="space-y-2">
            <Label htmlFor="ip-address">ESP32 IP Address</Label>
            <div className="flex gap-2">
                <Input 
                  id="ip-address" 
                  value={ipAddress} 
                  onChange={(e) => setIpAddress(e.target.value)} 
                  placeholder="e.g., 192.168.4.1" 
                  className="bg-input border-border" 
                  disabled={isConnected || isDetectingIP} 
                />
                <Button 
                  onClick={detectESP32IP} 
                  variant="outline" 
                  size="icon"
                  disabled={isConnected || isDetectingIP}
                  className="shrink-0"
                >
                  <RefreshCw className={`h-4 w-4 ${isDetectingIP ? 'animate-spin' : ''}`} />
                </Button>
                <Button onClick={handleConnect} variant={isConnected ? "destructive" : "default"} className="w-[120px]">
                    {isConnected ? <><ZapOff /> Disconnect</> : <><Zap /> Connect</>}
                </Button>
            </div>
             <p className={`text-xs pl-1 text-muted-foreground`}>
                {isDetectingIP ? 'Scanning for ESP32...' : 'Click refresh to auto-detect ESP32 IP address'}
            </p>
        </div>
        
        <div className={`space-y-6 transition-opacity ${!isConnected ? 'opacity-50 pointer-events-none' : 'opacity-100'}`}>
            <div className="space-y-2">
                <Label htmlFor="speed">Animation Speed</Label>
                <div className="flex items-center gap-4">
                    <span className="text-sm text-foreground">Slow</span>
                    <Slider
                        id="speed"
                        min={20}
                        max={500}
                        step={10}
                        value={[520 - speed]}
                        onValueChange={handleSpeedChange}
                        className="[&>span:first-child>span]:bg-accent"
                    />
                    <span className="text-sm text-foreground">Fast</span>
                </div>
            </div>
            
            <div className="flex items-center justify-around gap-4 pt-2">
                <div className="flex flex-col items-center gap-2">
                    <Label className="text-muted-foreground">Playback</Label>
                    <Button
                        variant="outline"
                        size="icon"
                        onClick={handlePlayPause}
                        className="h-12 w-12 rounded-full border-accent text-accent hover:bg-accent/20 hover:text-accent"
                        aria-label={isPlaying ? "Pause" : "Play"}
                    >
                        {isPlaying ? <Pause className="h-6 w-6" /> : <Play className="h-6 w-6" />}
                    </Button>
                </div>
                <div className="flex flex-col items-center gap-2">
                    <Label htmlFor="color-picker" className="text-muted-foreground">LED Color</Label>
                    <Input
                        id="color-picker"
                        type="color"
                        value={color}
                        onChange={handleColorChange}
                        className="w-12 h-12 p-1 bg-transparent border-border rounded-full cursor-pointer"
                    />
                </div>
            </div>

            <Button onClick={handleUploadSequence} className="w-full bg-accent hover:bg-accent/80 text-black font-bold text-lg py-6" disabled={isUploading}>
                <UploadCloud className="mr-2" />
                {isUploading ? 'Uploading...' : 'Upload Sequence to Curtain'}
            </Button>
        </div>
      </CardContent>
    </Card>
  );
}