local result = require("./ambiguous/luwon_file/dependency")
result[#result+1] = "required into module"
return result
