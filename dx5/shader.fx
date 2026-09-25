cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
	float3 gCameraPosition;
};

struct VertexData
{
	float3 PosL  : POSITION;
	float2 UV : TEXCOORD;
};

/*----------------VERTEX SHADER------------------------------------------*/
VertexData VS_Main(VertexData vin)
{
	VertexData vout;
	vout.PosL = vin.PosL;
	vout.UV = vin.UV;
	return vout;
}

struct PatchTess
{
	float EdgeTess[4] : SV_TessFactor;
	float InsideTess[2] : SV_InsideTessFactor;
};

/*----------------CONSTANT HULL SHADER------------------------------------------*/
// Funkcja ustawiaj¹ca sta³e wspó³czynniki teselacji dla patcha
PatchTess ConstantHS(InputPatch<VertexData, 4> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess patchTess;
    //patchTess.EdgeTess[0] = 1.0f; // Teselacja krawêdzi 0
    //patchTess.EdgeTess[1] = 1.0f; // Teselacja krawêdzi 1
    //patchTess.EdgeTess[2] = 1.0f; // Teselacja krawêdzi 2
    //patchTess.EdgeTess[3] = 1.0f; // Teselacja krawêdzi 3

    //patchTess.InsideTess[0] = 1.0f; // Teselacja wnêtrza w osi U
    //patchTess.InsideTess[1] = 1.0f; // Teselacja wnêtrza w osi V

    //Zadanie 2.1a
    //patchTess.EdgeTess[0] = 4.0f;
    //patchTess.EdgeTess[1] = 4.0f;
    //patchTess.EdgeTess[2] = 4.0f;
    //patchTess.EdgeTess[3] = 4.0f;

    //patchTess.InsideTess[0] = 4.0f;
    //patchTess.InsideTess[1] = 4.0f;
    
    //Zadanie 2.1b
    // Przyk³ad: mocna teselacja wzd³u¿ jednej krawêdzi
    //patchTess.EdgeTess[0] = 8.0f;
    //patchTess.EdgeTess[1] = 2.0f;
    //patchTess.EdgeTess[2] = 1.0f;
    //patchTess.EdgeTess[3] = 4.0f;

    //patchTess.InsideTess[0] = 5.0f;
    //patchTess.InsideTess[1] = 2.0f;
    
    // Zadanie 2.1c
    //Dynamiczne wartoœci
    //float base = 1.0f + (patchID % 5); // daje wartoœci od 1 do 5
    //patchTess.EdgeTess[0] = base;
    //patchTess.EdgeTess[1] = base + 1;
    //patchTess.EdgeTess[2] = base + 2;
    //patchTess.EdgeTess[3] = base + 3;
    //patchTess.InsideTess[0] = base;
    //patchTess.InsideTess[1] = base;
    
    //Zadanie 2.2
    // Œrodek obiektu (domyœlnie scena rysowana wokó³ (0,0,0))
    float3 objectCenter = float3(0.0f, 0.0f, 0.0f);

    // Oblicz dystans kamery od obiektu
    float distanceToCamera = distance(gCameraPosition, objectCenter);

    // Im bli¿ej, tym wiêksza teselacja
    float tessFactor = lerp(64.0f, 4.0f, saturate(distanceToCamera / 10.0f));

    patchTess.EdgeTess[0] = tessFactor;
    patchTess.EdgeTess[1] = tessFactor;
    patchTess.EdgeTess[2] = tessFactor;
    patchTess.EdgeTess[3] = tessFactor;
    patchTess.InsideTess[0] = tessFactor;
    patchTess.InsideTess[1] = tessFactor;
    
    return patchTess; // Zwraca wspó³czynniki teselacji
}

struct HullOut
{
	float3 PosL : POSITION;
	float2 uv : TEXCOORD0;
};

/*----------------HULL SHADER------------------------------------------*/
// Hull Shader – przekazuje dane dla ka¿dego punktu kontrolnego patcha
[domain("quad")] // Pracujemy na patchach typu quad
[partitioning("integer")] // Integer partitioning (jednostajny podzia³)
[outputtopology("triangle_cw")] // Wyjœciowe trójk¹ty w kolejnoœci zgodnej z ruchem wskazówek zegara
[outputcontrolpoints(4)] // 4 punkty kontrolne
[patchconstantfunc("ConstantHS")] // Funkcja zwracaj¹ca wspó³czynniki teselacji
[maxtessfactor(64.0f)] // Maksymalny wspó³czynnik teselacji
HullOut HS_Main(InputPatch<VertexData, 4> p, uint i : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    HullOut hout;
    hout.PosL = p[i].PosL; // Przepisanie pozycji danego punktu kontrolnego
    hout.uv = p[i].UV; // Przepisanie UV danego punktu kontrolnego
    return hout;
}

struct DomainOut
{
	float4 PosH : SV_POSITION;
	float2 uv : TEXCOORD0;
};

/*----------------DOMAIN SHADER------------------------------------------*/
Texture2D    gTexture1 : register(t0);
SamplerState gSampler1  : register(s0);
// Domain Shader – oblicza pozycjê wierzcho³ka wynikowego i UV
//[domain("quad")] // Patch ma kszta³t quad'a (czworok¹ta)
//DomainOut DS_Main(PatchTess patchTess, float2 uv : SV_DomainLocation, const OutputPatch<HullOut, 4> quad)
//{
//    DomainOut dout;

//    // Interpolacja pozycji wierzcho³ka wewn¹trz quad'a na podstawie wspó³rzêdnych domenowych
//    float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
//    float3 v2 = lerp(quad[3].PosL, quad[2].PosL, uv.x);
//    float3 p = lerp(v1, v2, uv.y);

//   // Deformacja w osi y
//    //p.y += sin(uv.x * 10.0f + uv.y * 10.0f) * 0.1f;
    
//    // Przekszta³cenie pozycji do przestrzeni ekranowej (clip space)
//    dout.PosH = mul(float4(p, 1.0f), gWorldViewProj);

//    //Do zadania 2.2
//    //dout.uv = 0.0f; // Zadanie 2.3: interpolacja UV do zaimplementowania
    
//    //Zadanie 2.3: interpolacja UV
//    float2 uv_v1 = lerp(quad[0].uv, quad[1].uv, uv.x);
//    float2 uv_v2 = lerp(quad[3].uv, quad[2].uv, uv.x);
//    dout.uv = lerp(uv_v1, uv_v2, uv.y);
    
//    return dout;
//}

// Zadanie 2.4
[domain("quad")] // Patch ma kszta³t quad'a (czworok¹ta)
DomainOut DS_Main(PatchTess patchTess, float2 uv : SV_DomainLocation, const OutputPatch<HullOut, 4> quad)
{
    DomainOut dout;

    float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
    float3 v2 = lerp(quad[3].PosL, quad[2].PosL, uv.x);
    float3 p = lerp(v1, v2, uv.y);

    // Interpolacja UV (potrzebna do przemieszczenia + przekazania do PS)
    float2 uv_v1 = lerp(quad[0].uv, quad[1].uv, uv.x);
    float2 uv_v2 = lerp(quad[3].uv, quad[2].uv, uv.x);
    dout.uv = lerp(uv_v1, uv_v2, uv.y);

    // Próbkowanie tekstury jako mapy wysokoœci
    float height = gTexture1.SampleLevel(gSampler1, dout.uv, 0).r;

    // Modyfikacja wspó³rzêdnej Y (przemieszczenie)
    p.y += height * 0.3f; // skaluj jak potrzebujesz (0.3 to przyk³ad)

    dout.PosH = mul(float4(p, 1.0f), gWorldViewProj);

    return dout;
}


/*----------------PIXEL SHADER------------------------------------------*/
float4 PS_Main(DomainOut pin) : SV_Target
{
    //Do zadania 2.2
	//return  float4(1.0f, 0.0f, 0.0f, 1.0f); //Zadanie 2.3 wpolrzedne UV
    
    //Zadanie 2.3
    return gTexture1.Sample(gSampler1, pin.uv);
}