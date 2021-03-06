function make_d, path=path
 
if not keyword_set(PATH) then path='' 

ni=collect(path=path, var="Ni") 
phi=collect(path=path, var="phi")
ni=ni[2,*,*,*] 
phi=phi[2,*,*,*] 


du=file_import("uedge.grd_beta.nc")
new_zmax=collect(path=path, var="ZMAX");-fraction of 2PI
rho_s = collect(path=path, var="rho_s")
wci  = collect(path=path, var="wci")
t_array = collect(path=path, var="t_array")

zeff = collect(path=path, var="Zeff")
AA = collect(path=path, var="AA")

old_zmax=new_zmax/(rho_s/du.hthe0)
print, 'new_zmax=', new_zmax, "; old_zmax=", old_zmax

d={ni_xyzt:ni, phi_xyzt:phi, rho_s:rho_s, zmax:old_zmax, Zeff:zeff, AA:AA, t_array:t_array, wci:wci}

return,d
end
