-- A path segment is turned into an area name no fetch can derive.
core.register_fetches("otel_area", function(txn)
    local path, seg = txn.f:path(), nil

    if (path ~= nil) then
        seg = path:match("^/([^/]+)")
    end

    if ((seg == "api") or (seg == "graphql")) then
        return "service"
    end
    return "site"
end)

-- The digits of the last path segment are masked.
core.register_converters("otel_tail", function(value)
    local head, tail = value:match("^(.*/)([^/]*)$")

    if ((head == nil) or (tail == nil)) then
        return value
    end
    return head .. tail:gsub("%d+", "N")
end)

-- The request method goes into a transaction variable.
core.register_action("otel_tag", { "http-req" }, function(txn)
    txn:set_var("txn.otel_method", txn.f:method())
end)
