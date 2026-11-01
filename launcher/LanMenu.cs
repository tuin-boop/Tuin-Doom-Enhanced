using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

internal sealed class LanLaunch {
    internal string Arguments;
}
internal sealed class LanMenu : Form {
    const int LobbyPort=5030;
    readonly string signature;
    readonly List<string> maps;
    readonly List<Peer> peers=new List<Peer>();
    readonly List<TcpClient> connections=new List<TcpClient>();
    TcpListener listener;
    UdpClient discovery,gameReservation;
    bool closing,hosting,starting;
    readonly Timer lobbyWatch=new Timer{Interval=500};
    int gamePort;
    string hostAddress;
    Label status=new Label();
    ListBox rooms=new ListBox(),people=new ListBox();
    TextBox address=new TextBox(),nickname=new TextBox();
    ComboBox map=new ComboBox(),mode=new ComboBox(),skill=new ComboBox();
    Button host,join,start,refresh;
    internal LanLaunch Launch;
    sealed class Peer { internal TcpClient Client;internal string Name,Address;internal int Port; }
    sealed class Room { internal string Address,Title;public override string ToString(){return Title+"  ("+Address+")";} }
    static string Encode(string s){return Convert.ToBase64String(Encoding.UTF8.GetBytes(s));}
    static string Decode(string s){return Encoding.UTF8.GetString(Convert.FromBase64String(s));}
    static async Task<string> Read(TcpClient c){
        var b=new List<byte>();var one=new byte[1];var stream=c.GetStream();
        while(b.Count<4096){int n=await stream.ReadAsync(one,0,1);if(n==0)throw new IOException("The other player disconnected.");if(one[0]==10)return Encoding.UTF8.GetString(b.ToArray()).TrimEnd('\r');b.Add(one[0]);}
        throw new IOException("Invalid lobby message.");
    }
    static async Task Send(TcpClient c,string s){var b=Encoding.UTF8.GetBytes(s+"\n");await c.GetStream().WriteAsync(b,0,b.Length);}
    static async Task<T> Deadline<T>(Task<T> task,int seconds){if(await Task.WhenAny(task,Task.Delay(seconds*1000))!=task)throw new IOException("Connection timed out. Check the host address and allow the game on your private network in Windows Firewall.");return await task;}
    Label LabelAt(string text,int x,int y,int w){var l=new Label{Text=text,Left=x,Top=y,Width=w,Height=24,ForeColor=ForeColor};Controls.Add(l);return l;}
    Button ButtonAt(string text,int x,int y,int w,Action action){var b=new Button{Text=text,Left=x,Top=y,Width=w,Height=34,FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(42,57,44),ForeColor=ForeColor};b.Click+=(s,e)=>action();Controls.Add(b);return b;}
    internal LanMenu(string game,string[] files,string fingerprint){
        signature=fingerprint;maps=MapNames(game,files);
        Text="Tuin-Doom-Enhanced / LAN Multiplayer";ClientSize=new Size(760,590);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;MinimizeBox=false;StartPosition=FormStartPosition.CenterParent;
        BackColor=Color.FromArgb(17,24,21);ForeColor=Color.FromArgb(227,235,224);Font=new Font("Segoe UI",10);
        LabelAt("LAN MULTIPLAYER  /  2–4 PLAYERS",22,16,700).Font=new Font(Font,FontStyle.Bold);
        LabelAt("Player name",22,53,125);nickname.SetBounds(150,50,220,28);nickname.Text=Environment.UserName;nickname.MaxLength=20;Controls.Add(nickname);
        LabelAt("HOST A GAME",22,97,300);
        map.SetBounds(22,124,155,30);mode.SetBounds(187,124,185,30);skill.SetBounds(382,124,195,30);
        foreach(var c in new[]{map,mode,skill}){c.DropDownStyle=ComboBoxStyle.DropDownList;Controls.Add(c);}
        map.Items.AddRange(maps.ToArray());map.SelectedIndex=0;mode.Items.AddRange(new[]{"Cooperative","Deathmatch","Deathmatch 2"});mode.SelectedIndex=0;
        skill.Items.AddRange(new[]{"I'm too young to die","Hey, not too rough","Hurt me plenty","Ultra-Violence","Nightmare!"});skill.SelectedIndex=3;
        host=ButtonAt("Host game",587,122,150,BeginHost);
        LabelAt("FIND / JOIN A GAME",22,171,400);refresh=ButtonAt("Find LAN games",552,164,185,Discover);
        rooms.SetBounds(22,202,715,100);rooms.BackColor=Color.FromArgb(29,38,32);rooms.ForeColor=ForeColor;Controls.Add(rooms);rooms.SelectedIndexChanged+=(s,e)=>{var r=rooms.SelectedItem as Room;if(r!=null)address.Text=r.Address;};
        address.SetBounds(22,313,510,28);address.Text="";Controls.Add(address);join=ButtonAt("Join selected / IP",552,309,185,BeginJoin);
        LabelAt("Enter the host's IPv4 address if discovery is unavailable.",22,346,715);
        people.SetBounds(22,378,510,75);people.BackColor=rooms.BackColor;people.ForeColor=ForeColor;Controls.Add(people);
        start=ButtonAt("Start match",552,378,185,StartMatch);start.Enabled=false;
        ButtonAt("Back",552,420,185,()=>Close());
        status.SetBounds(22,464,715,55);status.Text="Choose the same IWAD and add-ons on every computer.";Controls.Add(status);
        LabelAt("LAN uses classic synchronized gameplay. Destruction, custom AI, lockdowns,\nphoto mode and slow motion are unavailable during a network match.",22,526,715).Height=52;
        lobbyWatch.Tick+=(s,e)=>{
            if(!hosting||starting)return;
            foreach(var p in peers.ToArray()){
                bool disconnected;
                try{disconnected=p.Client.Client.Poll(0,SelectMode.SelectRead)&&p.Client.Client.Available==0;}catch{disconnected=true;}
                if(disconnected){peers.Remove(p);p.Client.Close();UpdatePeople();status.Text=p.Name+" left the lobby. Waiting for players.";}
            }
        };
        lobbyWatch.Start();
        FormClosed+=(s,e)=>{closing=true;lobbyWatch.Stop();lobbyWatch.Dispose();if(listener!=null)listener.Stop();if(discovery!=null)discovery.Close();if(gameReservation!=null)gameReservation.Close();foreach(var c in connections)c.Close();};
    }
    internal static List<string> MapNames(string game,string[] files){
        var names=new HashSet<string>();foreach(string path in new[]{game}.Concat(files)){
            if(!Path.GetExtension(path).Equals(".wad",StringComparison.OrdinalIgnoreCase))continue;
            using(var f=File.OpenRead(path))using(var r=new BinaryReader(f)){if(f.Length<12)continue;r.ReadInt32();int n=r.ReadInt32(),o=r.ReadInt32();if(n<0||n>100000||o<12||(long)o+n*16L>f.Length)continue;f.Position=o;
                for(int i=0;i<n;i++){r.ReadInt32();r.ReadInt32();string s=Encoding.ASCII.GetString(r.ReadBytes(8)).TrimEnd('\0');if(System.Text.RegularExpressions.Regex.IsMatch(s,"^(E[1-9]M[1-9]|MAP[0-9]{2})$"))names.Add(s);}}
        }return names.OrderBy(s=>s,StringComparer.Ordinal).ToList();
    }
    internal static string Fingerprint(string engine,string game,string[] files){
        var hashes=new List<string>();foreach(string path in new[]{engine,game}.Concat(files)){
            using(var sha=SHA256.Create())using(var f=File.OpenRead(path))hashes.Add(Convert.ToBase64String(sha.ComputeHash(f)));
            if(Path.GetExtension(path).Equals(".wad",StringComparison.OrdinalIgnoreCase)){foreach(string ext in new[]{".deh",".bex"}){string side=Path.ChangeExtension(path,ext);if(File.Exists(side))using(var sha=SHA256.Create())using(var f=File.OpenRead(side))hashes.Add(Convert.ToBase64String(sha.ComputeHash(f)));}}
        }
        using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(String.Join("|",hashes)))).Replace("-","");
    }
    void Reserve(){gameReservation=new UdpClient(new IPEndPoint(IPAddress.Any,0));gamePort=((IPEndPoint)gameReservation.Client.LocalEndPoint).Port;}
    void Lock(){host.Enabled=join.Enabled=refresh.Enabled=false;map.Enabled=mode.Enabled=skill.Enabled=nickname.Enabled=false;}
    void Error(Exception e){if(!closing){status.Text=e.Message;MessageBox.Show(this,e.Message,"LAN Multiplayer",MessageBoxButtons.OK,MessageBoxIcon.Information);}}
    void UpdatePeople(){people.Items.Clear();people.Items.Add(nickname.Text+" (host)");foreach(var p in peers)people.Items.Add(p.Name+" — ready");start.Enabled=hosting&&!starting&&peers.Count>0;}
    async void BeginHost(){
        try{Reserve();listener=new TcpListener(IPAddress.Any,LobbyPort);listener.Start();discovery=new UdpClient(LobbyPort);hosting=true;Lock();UpdatePeople();
            status.Text="Waiting for players. Host address: "+String.Join(" / ",Dns.GetHostAddresses(Dns.GetHostName()).Where(a=>a.AddressFamily==AddressFamily.InterNetwork).Select(a=>a.ToString()));
            DiscoveryHost();await AcceptPeers();
        }catch(Exception e){Error(e);if(!hosting){if(listener!=null)listener.Stop();if(discovery!=null)discovery.Close();if(gameReservation!=null)gameReservation.Close();}}
    }
    async Task AcceptPeers(){while(!closing){var c=await listener.AcceptTcpClientAsync();connections.Add(c);AcceptPeer(c);}}
    async void AcceptPeer(TcpClient c){try{
        string[] hello=(await Deadline(Read(c),10)).Split('|');
        if(starting||peers.Count>=3)throw new IOException("This lobby is full or already starting.");
        if(hello.Length!=4||hello[0]!="TDELAN1"||hello[1]!=signature)throw new IOException("Build or game files differ. Use the same release, IWAD and add-ons in the same order.");
        int port;if(!Int32.TryParse(hello[2],out port)||port<1024||port>65535)throw new IOException("Invalid game port.");
        string name=Decode(hello[3]);if(name.Length>20)throw new IOException("Player name is too long.");
        var p=new Peer{Client=c,Name=name,Address=((IPEndPoint)c.Client.RemoteEndPoint).Address.ToString(),Port=port};
        if(hostAddress==null)hostAddress=((IPEndPoint)c.Client.LocalEndPoint).Address.ToString();
        peers.Add(p);await Send(c,"WAIT|"+map.Text+"|"+mode.SelectedIndex);UpdatePeople();
    }catch(Exception e){Send(c,"ERROR|"+Encode(e.Message)).ContinueWith(t=>c.Close());}}
    async void DiscoveryHost(){try{while(!closing){var packet=await discovery.ReceiveAsync();if(!starting&&Encoding.ASCII.GetString(packet.Buffer)=="TDELAN1?"){
        string text="TDELAN1|"+Encode(nickname.Text+" / "+map.Text+" / "+(peers.Count+1)+" of 4")+"|"+signature;
        var b=Encoding.UTF8.GetBytes(text);await discovery.SendAsync(b,b.Length,packet.RemoteEndPoint);
    }}}catch{}}
    async void Discover(){refresh.Enabled=false;rooms.Items.Clear();try{using(var udp=new UdpClient(0)){udp.EnableBroadcast=true;var b=Encoding.ASCII.GetBytes("TDELAN1?");await udp.SendAsync(b,b.Length,new IPEndPoint(IPAddress.Broadcast,LobbyPort));
        DateTime until=DateTime.UtcNow.AddSeconds(2);var seen=new HashSet<string>();
        while(DateTime.UtcNow<until){var task=udp.ReceiveAsync();if(await Task.WhenAny(task,Task.Delay(Math.Max(1,(int)(until-DateTime.UtcNow).TotalMilliseconds)))!=task)break;
            var r=await task;string[] f=Encoding.UTF8.GetString(r.Buffer).Split('|');string ip=r.RemoteEndPoint.Address.ToString();if(f.Length==3&&f[0]=="TDELAN1"&&seen.Add(ip))rooms.Items.Add(new Room{Address=ip,Title=Decode(f[1])+(f[2]==signature?"":" [different files]")});
        }
        status.Text=rooms.Items.Count>0?"Select a game, then Join.":"No hosts found. You can also enter the host's IPv4 address.";
    }}catch(Exception e){Error(e);}finally{if(!closing&&!hosting)refresh.Enabled=true;}}
    async void BeginJoin(){TcpClient c=null;try{
        IPAddress ip;if(!IPAddress.TryParse(address.Text.Trim(),out ip)||ip.AddressFamily!=AddressFamily.InterNetwork)throw new IOException("Enter the host's IPv4 address, for example 192.168.1.20.");
        Reserve();Lock();c=new TcpClient();connections.Add(c);var connect=c.ConnectAsync(ip,LobbyPort);if(await Task.WhenAny(connect,Task.Delay(8000))!=connect)throw new IOException("Host did not respond. Check its address and private-network firewall access.");await connect;
        await Send(c,"TDELAN1|"+signature+"|"+gamePort+"|"+Encode(nickname.Text));
        while(!closing){string line=await Read(c);string[] f=line.Split('|');
            if(f[0]=="ERROR")throw new IOException(Decode(f[1]));
            if(f[0]=="WAIT"){status.Text="Connected. Waiting for the host to start "+f[1]+".";people.Items.Clear();people.Items.Add(nickname.Text+" — ready");}
            else if(f[0]=="PLAY"){
                var launch=ParseLaunch(f,gamePort);await Send(c,"READY");string go=await Deadline(Read(c),15);if(go!="GO")throw new IOException("Host cancelled the start.");Launch=launch;DialogResult=DialogResult.OK;Close();return;
            }
        }
    }catch(Exception e){if(c!=null)c.Close();if(gameReservation!=null)gameReservation.Close();Error(e);if(!closing){host.Enabled=join.Enabled=refresh.Enabled=true;map.Enabled=mode.Enabled=skill.Enabled=nickname.Enabled=true;}}}
    internal static LanLaunch ParseLaunch(string[] f,int port){
        if(f.Length!=7||f[0]!="PLAY")throw new IOException("Invalid start message.");int me,skill,mode;
        var roster=f[6].Split(',');if(!Int32.TryParse(f[1],out me)||me<0||me>=roster.Length||roster.Length<2||roster.Length>4||!System.Text.RegularExpressions.Regex.IsMatch(f[2],"^[a-fA-F0-9]{32}$")||!System.Text.RegularExpressions.Regex.IsMatch(f[3],"^(E[1-9]M[1-9]|MAP[0-9]{2})$")||!Int32.TryParse(f[4],out skill)||skill<1||skill>5||!Int32.TryParse(f[5],out mode)||mode<0||mode>2)throw new IOException("Invalid match settings.");
        foreach(string e in roster){string[] parts=e.Split(':');IPAddress ip;int p;if(parts.Length!=2||!IPAddress.TryParse(parts[0],out ip)||ip.AddressFamily!=AddressFamily.InterNetwork||!Int32.TryParse(parts[1],out p)||p<1024||p>65535)throw new IOException("Invalid player address.");}
        string warp=f[3].StartsWith("MAP")?Int32.Parse(f[3].Substring(3)).ToString():f[3][1]+" "+f[3][3];
        return new LanLaunch{Arguments=" -net "+(me+1)+" "+String.Join(" ",roster.Where((s,i)=>i!=me))+" -port "+port+" -lan-token "+f[2]+" -skill "+skill+" -warp "+warp+(mode==1?" -deathmatch":mode==2?" -altdeath":"")};
    }
    async void StartMatch(){if(starting||peers.Count==0)return;starting=true;start.Enabled=false;status.Text="Starting match...";try{
        string token=Guid.NewGuid().ToString("N");var roster=new[]{hostAddress+":"+gamePort}.Concat(peers.Select(p=>p.Address+":"+p.Port)).ToArray();
        string common="|"+token+"|"+map.Text+"|"+(skill.SelectedIndex+1)+"|"+mode.SelectedIndex+"|"+String.Join(",",roster);
        for(int i=0;i<peers.Count;i++)await Send(peers[i].Client,"PLAY|"+(i+1)+common);
        foreach(var p in peers)if(await Deadline(Read(p.Client),15)!="READY")throw new IOException("A player was not ready.");
        foreach(var p in peers)await Send(p.Client,"GO");Launch=ParseLaunch(("PLAY|0"+common).Split('|'),gamePort);DialogResult=DialogResult.OK;Close();
    }catch(Exception e){Error(e);status.Text="Match start failed. Close this lobby and host again.";}}
}

