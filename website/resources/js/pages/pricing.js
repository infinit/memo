$(document).ready(function() {

  function updatePrice() {

  }

  $("#usersInput").bind("slider:changed", function (event, data) {
    var users = $('.estimation table .users td span');
    var usersLabel = $('#usersLabel span');
    users.text(data.value);
    usersLabel.text(data.value);
  });

  $("#capacityInput").bind("slider:changed", function (event, data) {
    var capacities = $('.estimation table .capacity td span');
    var capacityLabel = $('#capacityLabel span');
    capacities.text(data.value);
    capacityLabel.text(data.value);
  });

});