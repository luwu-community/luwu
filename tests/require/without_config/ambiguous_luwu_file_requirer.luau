local result = require("./ambiguous/luwu_file/dependency")
result[#result+1] = "required into module"
return result
